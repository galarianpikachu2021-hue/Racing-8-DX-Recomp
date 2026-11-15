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
