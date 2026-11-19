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

### The race-start crash: found and fixed (2026-11-15)

**Cause: SetThreadCoreMask on a thread that was not started yet was dropped.** The game creates its worker threads, sets their core
masks ("core 2 only" and so on) and only then starts them. A thread gets its scheduler entry at StartThread, so the runtime chose
the new core but recorded nothing, and every worker started on the core it was created for: core 0, outside its own mask.

The game noticed in the worst way. At its start each thread stores `GetCurrentCoreNumber() + 1` in a TLS slot (main+0x97c9f4), and
that value picks a per-core work buffer (main+0x7205f4). All threads read core 0, so they all shared buffer 0. When the race starts
and several threads fill those buffers at once, the main thread read another thread's records as its own object pointers: a garbage
`this` (0x1000000000011) passed to an NVN entry point, a call to null, then a crash.

How it was found:
1. The guest registers from the core dump (block context in host `rbx`) and a frame walk through the direct-memory arena led to
   the game's call site.
2. The record layout showed the array was another structure.
3. The import names (from main's PLT relocations) showed the per-core buffer and its TLS index.
4. Reading every thread's TLS value from the dump showed they were all 1.

Fix: `GuestThread::start_core` (set by CreateThread and by SetThreadCoreMask before the start); test in `test_game_kernel.cpp`.

**Result:** the runtime kernel (`KARTRECOMP_NATIVE_KERNEL=1`) now matches the default mode in every check:

| check | result |
|---|---|
| 1P race, 4.0.1 | 200 s, 60 fps, laps 1-2 render correctly |
| 1P race, 3.0.4 | 150 s, 60 fps |
| 4P race | 60.0 fps on average, no slow seconds (default mode: 59.9) |
| keyless boot | races |

The race-start dips (51-55 fps) are gone, because the workers now really run on their own cores. No call-to-null recoveries,
no core dumps. Still opt-in until the user has played it.

## Default on Linux (2026-11-18)

The runtime kernel is now the Linux default (`KARTRECOMP_NATIVE_KERNEL=0` brings back the emulated kernel; other platforms stay
opt-in with `=1`). Checks with the current engine:

| check | result |
|---|---|
| 1P race, 4.0.1, no mods | 150 s, 59 fps, no errors |
| CTGP DX, 3.0.4 (cup screen, music switch, banner, speedometer) | 59 fps; CTGP's guest arena now maps through the runtime's address space |
| Setup's test boot (no picture, `KARTRECOMP_VERIFY_BOOT`) | 9.8 billion blocks in 45 s, keeps running (passes the new main-loop rule) |
| Wireless Play over a hosted room | same as the emulated kernel: room created, on to kart select |

Not re-run this time: 4P (passed on 2026-11-15, kernel code unchanged since), keyless boot. Next: stage 5 (audio_core, hid_core
and the driver module on runtime events), then stage 6 (delete `core/hle/kernel` from the Linux build).

## Stages 5-6: what is left and in which order (2026-11-18)

On the runtime kernel the emulated kernel no longer schedules anything (its 10 ms preemption pass is off too). What still uses it:

1. **Boot**: `KernelCore::Initialize` (slab heaps, memory layout, `KMemoryManager` whose application pool the runtime takes over),
   `KProcess::LoadFromMetadata` + module loading, then `NativeGame::ImportProcess` copies the mapped modules into the runtime's
   address space.
2. **The host-memory view**: `Core::Memory` reads the page table owned by `KProcessPageTable`; the runtime's `AddressSpace`
   backend maps through it.
3. **Library objects**: `KEvent`/`KReadableEvent` (audio_core, hid_core, nvdrv; handed to the game in 4 places in the native
   service shells through `MirrorKernelEvent`), `KSharedMemory` (hid, fonts), `KTransferMemory` (audio), `KProcess` (memory
   access, device locks).

Order:
- **5a** (done): no emulated preemption on the runtime kernel.
- **5b, library objects**: a small `lib_objects` layer (event = signal state + runtime mirrors; shared memory = host buffer +
  runtime physical range; transfer memory = runtime address/size) with the emulated kernel's objects behind it while other
  platforms still use the emulated kernel. Creation goes through `ServiceContext::CreateEvent` (33 sites); about 150 use sites
  (Signal/Clear/GetReadableEvent) in the kept libraries.
- **2, runtime loader**: modules from the loader's code sets mapped with `AddressSpace::MapNew` + copy + permissions; physical
  memory from the runtime's allocator, not `KMemoryManager`.
- **Own memory view**: `Core::Memory` on a page table owned by the runtime (the backend already does all the mapping calls).
- **6, delete**: `core/hle/kernel` out of the build. Precondition for doing it everywhere instead of Linux-only: the runtime
  kernel tested on Windows (COOPERATE.md asks the Windows session).

None of this changes what the player sees; it removes code and threads. Expected size: several sessions.

## Stage 6, first cut done; the scheduler cannot go yet (2026-11-18)

Done: with `KARTRECOMP_RUNTIME_KERNEL_ONLY` (CMake option, default on for Linux) the emulated system-call handlers
(`hle/kernel/svc.cpp` + 41 `svc/svc_*.cpp`) are not built, the runtime kernel is always on, and the three callers that named them
(worker-thread exit, time-service retry sleep, service-manager wait) do the same work directly. `local/exp/nkcheck.sh <label>`
runs the four standard checks; all pass (1P 4.0.1 race 57-59 fps, CTGP DX on 3.0.4 with switch/speedometer/cups, Setup's test boot,
Wireless Play over a hosted room).

Tried and reverted: also dropping `physical_core`, `k_scheduler`, `global_scheduler_context`, `k_hardware_timer`,
`k_thread_queue`, `k_light_lock`, `k_condition_variable`, `k_address_arbiter`, `k_synchronization_object`. The link fails with ~900
undefined references, nearly all `KLightLock::Lock/Unlock` (375) and `KScheduler::DisableScheduling/EnableScheduling/
UpdateHighestPriorityThreads` (309): every kept kernel object (`KEvent`, `KReadableEvent`, `KSharedMemory`, `KTransferMemory`,
`KProcess`, `KThread` for host threads, the handle table) takes a `KScopedSchedulerLock`. So the scheduler stays until those
objects stop being used by the libraries.

What that needs (stage 5b, not started, several sessions): replace the library-facing kernel objects with plain classes in
`kart_runtime` (events: signal state + runtime mirrors, created in `ServiceContext::CreateEvent`, 33 creation sites, ~150
Signal/Clear/GetReadableEvent uses across audio_core, hid_core, nvdrv, the hid/audio shells; shared and transfer memory for hid,
fonts and audio; process access for memory reads and device locks). Only after that do `KThread`, the scheduler, `KernelCore` and
the memory manager have no users, and the loader plus `Core::Memory`'s page table move to the runtime (stages 2 and "own memory
view"). Doing it in one pass without a way to run the audio and controller paths after every file would break the game, so it
should be done event type by event type with `nkcheck.sh` after each.

## Stage 6 done for Linux: the emulated kernel's machinery is out of the build (2026-11-19)

`KARTRECOMP_RUNTIME_KERNEL_ONLY` (CMake option, on by default for Linux; Windows and Android keep the emulated kernel until the
runtime kernel is tested there, COOPERATE.md) builds the engine with the game always on the runtime kernel. In order:

| step | left out of the build | stands in |
|---|---|---|
| system calls | `svc.cpp` + 41 `svc/svc_*.cpp` | the runtime answers every call |
| scheduling | `physical_core`, `k_scheduler`, `global_scheduler_context`, `k_hardware_timer`, `k_thread_queue`, `k_light_lock`, `k_condition_variable`, `k_address_arbiter`, `k_synchronization_object` | `k_runtime_lite.cpp`: scheduler lock = recursive lock + dispatch count, light lock on its tag word, host-side waits check and sleep |
| IPC | `k_port`, `k_session`, `k_server_session`, `k_client_*`, `k_light_*session`, `k_session_request`, `k_code_memory`, `k_device_address_space`, `k_interrupt_manager`, `k_lock_trace` | nothing: the runtime answers every service; the registry keeps library objects without ports; framework IPC paths are fenced |
| memory view | (the emulated process page table) | the runtime owns the `Common::PageTable` the recompiled code and `Core::Memory` read |
| loading | `k_process.cpp` | `k_process_lite.cpp` + `NativeGame::Prepare/LoadModule`: modules go straight into the runtime's address space |
| page tables | `k_page_table_base`, `k_memory_block_manager`, `k_thread_local_page` | `k_page_table_lite.cpp`: queries answer from the runtime's memory map |
| physical memory | `k_memory_manager`, `k_page_heap`, `k_memory_layout` (+ board), `k_page_buffer`, `k_system_resource` | `k_memory_manager_lite.h` / `k_memory_lite.cpp`: fixed pools in device memory (the game's 3285 MiB pool, was 3248), slab heaps in host memory |
| threads | `k_thread.cpp` | `k_thread_lite.cpp`: records only (host threads, per-core records), no fibers |

About 25,000 lines of the emulated kernel are no longer built; ~5,400 remain: the object model the kept libraries use (events,
shared and transfer memory, handle table, resource limits, object registry, capabilities) and the kernel set-up, plus ~1,200 lines of
lite stand-ins. That is what the plan meant by "except what stage 5 keeps as plain code".

Checks after every step (`local/exp/nkcheck.sh`): 1P 4.0.1 race, CTGP DX on 3.0.4 (cups, music switch, banner, speedometer),
Setup's picture-less test boot, Wireless Play over a hosted room. At the end also a 4-player race: four views, 59.85 fps average,
no slow seconds. One rare hang in the CTGP menu flow (1 in 15 runs, at the menu movies) was seen once, before these steps were
finished, and not reproduced since; `local/exp/hangcatch.sh` saves thread stacks if it happens.

What is left of the plan: deleting the files for every platform (needs the Windows test), Phase 7 (native memory model: the
generator emitting plain pointer loads for guest VA == host VA) and the optional Phase 8.
