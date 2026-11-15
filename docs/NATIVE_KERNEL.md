# Native kernel: replacing suyu's kernel under the game (plan, 2026-11-15)

State before this work (docs/NATIVE_ENGINE.md): every service request, every wait, lock, sleep and event of the game is
answered by the runtime (`src/kart_runtime/`), no emulated service module runs, the emulated GPU is gone, and the unused service
code is out of the Linux build. What is still suyu's kernel (`core/hle/kernel`):

- the process: address-space layout and page-table bookkeeping (`KPageTable*`), physical page allocation
  (`KMemoryManager`), module loading (`KProcess::LoadFromMetadata/LoadModule`, `CodeSet`), TLS pages;
- the thread records (`KThread`) and their virtual-core plumbing (`PhysicalCore`, `ArmInterface::RunThread`), though the threads
  themselves run as host threads on the runtime's core scheduler;
- the system-call entry (`Svc::Call`) and the remaining calls: memory (SetHeapSize, Map/UnmapMemory, QueryMemory,
  SetMemoryAttribute, MapSharedMemory, CreateTransferMemory), threads (Create/Start/Exit, priority, core mask, id, processor),
  GetInfo, Break, the handle table for kernel objects;
- the kernel object types the kept library code uses: `KEvent`/`KReadableEvent` (audio_core, hid_core, the driver module),
  `KSharedMemory` (hid shared memory, fonts), `KTransferMemory` (audio), `KProcess` (audio, driver sessions, device address
  space locks).

## Stages

1. **Address space** (`kart_runtime/address_space.h`): the 39-bit layout the game gets today (logged by `KProcess`:
   space 0+0x80_0000_0000, code 0x800_0000, heap 0x21_0360_0000+8 GiB, alias 0x11_0360_0000+64 GiB, stack
   0x10_8360_0000+2 GiB, kernel map 0x8360_0000+64 GiB), a block map with state / permission / attribute / ipc and device
   counts, and the operations the game uses: query, heap size, map/unmap (alias), attributes, shared and transfer memory, code,
   stacks, TLS pages, device locks. Physical pages from a runtime allocator; host mappings through a backend interface (the
   adapter calls `Core::Memory::MapMemoryRegion/UnmapRegion/ProtectRegion`, the host-memory view the recompiled code reads).
   Standalone, tested.
2. **Process and loader**: the game's modules (rtld, main, subsdk*, sdk) mapped as Code / CodeData from the loader's code
   sets, main thread stack, TLS pages, the process's GetInfo values.
3. **Threads and executor**: game threads as runtime threads (id, priority, core mask, TLS, state) on the runtime's core
   scheduler, each running the recompiled code with its own register context (the recompiled-code runner without `KThread`).
4. **System-call table**: all calls the game makes, answered by the runtime.
5. **Library adaptation**: audio_core, hid_core and the driver module on runtime events, shared and transfer memory and process
   memory.
6. **Delete** `core/hle/kernel` (except what stage 5 keeps as plain code) and the service framework from the Linux build.

Each stage is checked with the scripted races (1P 4.0.1 / 3.0.4, 4P, keyless). Stages 2-4 switch the whole game process at once
(`KARTRECOMP_NATIVE_KERNEL=1` until it has parity, then default on Linux).

## Stage 1 done (2026-11-15)

`kart_runtime/address_space.h` + `tests/test_address_space.cpp` (also under ASan/UBSan): block map with merged queries, the
inaccessible area outside the space, code / data / TLS / stack pages from a physical allocator, heap in 2 MiB steps, alias
(source locked and hidden, destination Stack in the stack region), Uncached / PermissionLocked attributes with the kernel's
state rules, transfer locks, device lock counts, shared memory from foreign pages (not freed on unmap), free-range search.
The fake backend in the test keeps a page table over real bytes, so an alias is checked by reading the same data twice.

## The reference trace (2026-11-15)

`KARTRECOMP_KERNEL_TRACE=<file>` (in `Svc::Call`) records the memory, thread and info calls with arguments and results, and
the MemoryInfo QueryMemory writes. A 120 s race (`local/exp/runs/kernel-trace-401.txt`, local only): 2,297 calls - 855
SetMemoryAttribute (the GPU driver marking pool memory uncached, mask/value 8), 1,041 QueryMemory (walks over the modules at
boot), 53 CreateThread (core -2 = the ideal core) with 53 MapMemory (the SDK aliases each thread's stack from the heap into the
stack region) and 16 UnmapMemory, 50 StartThread, 22 SetThreadCoreMask, 37 GetCurrentProcessorNumber, 85 GetThreadId, 41
CloseHandle, 2 MapSharedMemory (hid, fonts), 2 CreateTransferMemory (audio, driver), one SetHeapSize (0xc1c00000), 37 GetInfo:
core mask 7, alias region / size, heap region / size, ASLR region 0x800_0000 + 0x7f_f800_0000, stack region / size, total
memory 0xcd50_0000, used memory, random entropy 0-3, idle ticks 0. The region positions move between runs (suyu places them
from the code's end), so the runtime can use its own fixed layout.

## Stages 3-4, runtime side (2026-11-15)

`kart_runtime/game_kernel.h` + `tests/test_game_kernel.cpp` (ASan/UBSan and TSan clean): `GuestThread` (registers, TLS slot,
priority, ideal core, affinity, its handle is a waitable signalled at its end), `Executor` (runs a thread's code on a virtual
core until a system call or an interrupt), and `GameKernel`: the host-thread loop (take a core, run, answer the call, yield
on a preemption request), TLS pages of 8 slots in the kernel-map region, the main thread (stack in the stack region, x1 = its
handle), shutdown (every wait cancelled, code interrupted, host threads joined) and the system-call table for every call in
the reference trace plus the sync, IPC and wait calls: 0x01-0x10, 0x13-0x1F, 0x21, 0x24-0x27, 0x29, 0x34-0x35 (register
layouts from suyu's generated wrappers). The test drives it with a scripted executor: threads that create threads, alias
stacks, wait on addresses, wait for a thread's end, move cores, lock transfer memory, and a sleeper ended by shutdown.
Next: the adapter (stage 2 + executor) - the recompiled-code runner as an `Executor`, `Core::Memory` as the
`AddressSpace::Backend`, the application pool as physical memory, the modules from the loader, the service objects.

## First runs on the runtime kernel (2026-11-15)

With `KARTRECOMP_NATIVE_KERNEL=1` the game (4.0.1, no mods) now boots on the runtime's kernel, goes through the menus and races
at 55-60 fps for about 40 s. Then it crashes at race start (open, see below). Without the switch nothing changed (1P races on 4.0.1
and 3.0.4 checked).

What it took, in order found:

1. **Handles must not carry bit 30.** The SDK's lock words keep the owner thread's handle and use bit 30 as their "has waiters"
   flag. Runtime handles were marked with bit 30 (to tell them apart from the emulated kernel's), so as soon as threads were runtime
   objects every lock looked unowned and contended. The symptoms were 1.6M ArbitrateUnlock calls, and the SDK's
   `SdkRecursiveMutexType::IsLockedByCurrentThread` assertion fired inside `nn::prepo::detail::ipc::Initialize`
   (`SetTerminateResult` 2162-0001, then Break). Fix: `NativeKernel::SetPlainHandles()`. As the game's only kernel, handles are
   small numbers, as on the console; next to the emulated kernel bit 30 stays.
2. **Break ends the process** (reason bit 31 clear), after logging a frame-pointer backtrace (`on_break`). The thread used to
   loop on it.
3. **Host waits give the virtual core up.** The NVN host's frame pacing and the display queue call
   `NativeThreads::BeginHostWait()`, which only knew the emulated kernel's threads. A runtime game thread slept holding its core,
   and the threads sharing that core starved: the boot logo never finished, and audio ran at half rate. It now goes to
   `GameKernel::BeginWait/EndWait`, which nest (a blocking service call whose library code blocks again).
4. **One code runner per game thread** (`RecompExecutor` in `native_game.cpp`). With the process's four per-core runners, a
   thread that gave its core up in the middle of a host call came back with another thread's registers (pc 0xffffffff). Each runner:
   - takes the first runner's prepared module state (`ArmRecomp::AdoptPreparedState`). Its own relocation pass in the middle of
     the game would zero the relocation sizes of modules the game's loader has not relocated yet.
   - follows its thread's core (`SetCoreIndex`: exclusive-monitor slot).
   - has the core's reservation cleared when another runner takes the core, as a context switch does on the console.
5. Diagnostics:
   - `KARTRECOMP_NATIVE_KERNEL_TRACE` names the objects behind every WaitSynchronization (`wait 3f=event:kernel event`).
   - `Service::on_error` logs failing results.
   - `KARTRECOMP_NATIVE_SERVICES_TRACE` lists calls per command at exit.
   - The call-to-null recovery logs the guest callers and the object.

**Open: corruption at race start (~40 s).** The game passes a garbage object (`0x1000000000059`) to an NVN entry point, a global
function pointer called from main+0x68bf6c into the SDK's NVN code at sdk+0x2d5998 (`obj->[0x10]->vtable[7]`). The run then
crashes in that block (direct memory), sometimes after a few "call to null" recoveries at the same place. It only happens on the
runtime kernel. Ruled out so far:
- the relocation passes (now one)
- exclusive reservations
- heap block merging (merges require contiguous physical pages)
- the GPU driver's device mapping (it uses the host page table the runtime updates)

Candidates to look at next:
- the game's ~850 SetMemoryAttribute(uncached) calls on GPU pools
- transfer-memory protection (`LockTransfer` with permission None makes the audio buffer unreadable for the game *and* for
  audio_core's writes)
- shared-memory pages

Finding the culprit: the core dump has the guest registers in the block's context (host `rbx`), and guest memory is the direct
memory arena (logged at boot) plus the guest address, so the guest stack can be walked in gdb with a few lines of Python
(read 16 bytes at arena + x29: previous x29 and the return address).

### Second round (2026-11-15, later)

Fixed (all real, none of them was the race-start crash):
- **Moving a running thread to another core** (`CoreScheduler::SetCore`, used by both kernels' native threads). The old code handed
  the core on at once and asked for a preemption, but the interrupt went to the *destination* core's runner. The moved thread kept
  executing next to the core's new owner while reporting the new core's number. Now a running thread gets a pending move and
  keeps its core until it yields or waits (test `MoveRunningThread`).
- **Fresh pages are cleared** (`AddressSpace::Backend::Clear`, punched out of the backing as suyu's heap fill does). The pool's pages
  could hold old data.
- **x18 starts random and odd** in new threads, as the console's kernel sets it.
- Check: `GetCurrentProcessorNumber` from a thread that does not hold its core is logged. None seen.

Ruled out:
- the zero punch (`KARTRECOMP_ZERO_PUNCH=0` still crashes)
- direct-memory faults (none reported)
- sharing of the game's per-core buffers (`main+0x7205f4` uses `nn::os::GetTlsValue`, else `GetCurrentCoreNumber`, and
  svc 0x10 is called once per thread)
- waiting at the next system call instead of inside `nvnWindowAcquireTexture` (still crashes, and draws over the window texture
  on screen: flickering UI, reverted)

**Strongest clue:** with `KARTRECOMP_NVN_NO_PACING=1` (frames not paced, ~210-260 fps) the game plays the whole 12,000-frame script
through race start without a crash, twice. Paced at 60 fps it crashes at race start every time. So it depends on timing: something
that runs on wall-clock time (audio, the display's vsync, a streaming thread) meets race start.

Next:
- find which thread passes the bad object, by thread role, at race start
- compare thread priorities and core masks with the emulated kernel's for the same threads
