# Native engine: from the emulated console OS to our own runtime

Goal (user, 2026-11-12): no emulation left. The game's code is already recompiled and static (no JIT); what is still emulated is the
console OS underneath it (kernel, IPC, system services) and the guest memory model. The plan replaces the Nintendo SDK's *API*
(the `sdk` module's exported `nn::` functions, plus the C library and math functions it also provides) subsystem by subsystem with
host code, then deletes the emulator underneath. Phases: 0 measure the boundary (done, below); 1 C library, math, nn::os; 2 hid,
time, settings; 3 fs; 4 audio; 5 present/GPU cleanup (the emulated GPU is still created, unused); 6 remaining services, then delete
the HLE kernel, IPC and services; 7 native memory model (guest address = host address, plain loads in the generated code); 8
optional function-level codegen. Every step stays fully static (no JIT), sits behind a `KARTRECOMP_NATIVE_<PART>` switch until the
user has played it, and is checked with the TAS races (1P, 2P, 4P), screenshots and `local/exp/ab.sh` A/B runs.

Why at the SDK API: `main` calls into the separate `sdk` module, and those cross-module calls come back to the host dispatcher, where
`core/arm/recomp/host_hooks` can answer them (this is how the NVN graphics hooks already work, on 3.0.4 and 4.0.1). Calls inside
`main` cannot be hooked at run time in the 4.0.1 build (they are resolved in the generated code); calls inside `sdk` mostly not either.

## Phase 0: the measured boundary (2026-11-12)

Tool: `KARTRECOMP_SDK_TRACE=1` (`core/arm/recomp/sdk_trace.{h,cpp}` in the fork). It hooks every exported function of `sdk` (17,270)
and writes `<log dir>/sdk_trace.txt` every 10 s: calls from main, all calls, calls from main in the last 10 s, mangled name
(demangle with `c++filt`). Run: `local/exp/mprun2.sh 1 <label>` with `EXTRA_ENV=KARTRECOMP_SDK_TRACE=1` (boot, menus, an effects race;
the last 10 s window is in the race). Raw files: `local/exp/runs/sdk_trace-{401,304}.txt` (local).

### 4.0.1: 1549 sdk functions called; by group (calls from main, whole run / per second in the race)

| group | functions | calls (run) | calls/s (race) |
|---|---|---|---|
| libc | 336 | 66,111,936 | 304,292 |
| nn::os | 184 | 33,144,172 | 252,895 |
| libm (math) | 13 | 9,083,600 | 78,363 |
| nn::audio | 144 | 3,517,166 | 31,505 |
| nn::util | 25 | 824,792 | 4,636 |
| nn::hid | 82 | 284,902 | 2,005 |
| nn::mem | 11 | 159,486 | 716 |
| nn::fontll | 12 | 26,917 | 0 |
| nn::mii | 102 | 10,047 | 0 |
| nn::fs | 118 | 6,794 | 0 |
| nn::err | 3 | 1,285 | 0 |
| nn::time | 17 | 899 | 6 |
| nn::friends | 11 | 606 | 0 |
| nn::ldn | 1 | 513 | 0 |
| nn::prepo | 26 | 173 | 0 |
| nn::crypto | 18 | 38 | 0 |
| nn::oe | 15 | 21 | 0 |
| nn::lmem | 20 | 17 | 0 |
| nn::account | 10 | 13 | 0 |
| nn::settings | 4 | 8 | 0 |
| nn::vi | 28 | 6 | 0 |
| nn::swkbd | 3 | 3 | 0 |
| nn::pl | 3 | 3 | 0 |
| nn::aoc | 7 | 2 | 0 |
| other C++ (nv) | 2 | 2 | 0 |
| nn::nlibsdk | 22 | 1 | 0 |
| nn::apm | 3 | 1 | 0 |

- **libc**: `memcpy` 127,628/s, `__memcpy_device` 127,628/s, `memset` 28,617/s, `strcmp` 10,458/s, `memmove` 3,574/s, `__memmove_device` 3,574/s
- **nn::os**: `os::GetTlsValue` 164,689/s, `os::UnlockMutex` 17,438/s, `os::LockMutex` 17,319/s, `os::GetSystemTick` 9,758/s, `os::ClearLightEvent` 7,164/s, `os::SignalLightEvent` 6,845/s
- **libm (math)**: `cosf` 33,688/s, `sinf` 33,410/s, `atan2f` 6,484/s, `tanf` 1,835/s, `fmodf` 1,248/s, `asinf` 1,214/s
- **nn::audio**: `audio::SetVoiceMixVolume` 17,316/s, `audio::IsVoiceValid` 3,360/s, `audio::GetVoicePlayState` 1,681/s, `audio::IsVoiceDroppedFlagOn` 1,676/s, `audio::GetVoicePlayedSampleCount` 1,669/s, `audio::GetReleasedWaveBuffer` 1,669/s
- **nn::util**: `util::VSNPrintf` 2,640/s, `util::BinaryFileHeader::GetFirstBlock` 1,995/s, `util::BinaryFileHeader::IsRelocated` 0/s, `util::SNPrintf` 0/s, `util::BinaryFileHeader::GetFirstBlock` 0/s, `util::RelocationTable::Relocate` 0/s
- **nn::hid**: `hid::GetNpadStyleSet` 837/s, `hid::VibrationNode::GetCurrentVibration` 299/s, `hid::VibrationTarget::GetActualVibration` 238/s, `hid::VibrationNode::Update` 192/s, `hid::GetSixAxisSensorStates` 119/s, `hid::GetNpadState` 119/s
- **nn::mem**: `mem::StandardAllocator::Allocate` 417/s, `mem::StandardAllocator::Reallocate` 298/s, `mem::StandardAllocator::Allocate` 0/s, `mem::StandardAllocator::Free` 0/s, `mem::StandardAllocator::StandardAllocator` 0/s, `mem::StandardAllocator::StandardAllocator` 0/s
- **nn::fontll**: `fontll::ScalableFontEngine::CheckGlyphExist` 0/s, `fontll::ScalableFontEngine::SetScale` 0/s, `fontll::ScalableFontEngine::GetAdvance` 0/s, `fontll::ScalableFontEngine::GetKerning` 0/s, `fontll::ScalableFontEngine::SetFont` 0/s, `fontll::ScalableFontEngine::SetFlags` 0/s
- **nn::mii**: `mii::detail::CalculateSizeAndAlignment::AddSizeWithAlignment` 0/s, `mii::detail::CalculateSizeAndAlignment::AddSizeWithAlignment` 0/s, `mii::detail::CalculateSizeAndAlignment::~CalculateSizeAndAlignment` 0/s, `mii::detail::CalculateSizeAndAlignment::~CalculateSizeAndAlignment` 0/s, `mii::detail::CalculateSizeAndAlignment::GetSize` 0/s, `mii::detail::CalculateSizeAndAlignment::CalculateSizeAndAlignment` 0/s

### 3.0.4: 1549 sdk functions called; by group (calls from main, whole run / per second in the race)

| group | functions | calls (run) | calls/s (race) |
|---|---|---|---|
| libc | 336 | 64,841,882 | 276,204 |
| nn::os | 184 | 32,636,303 | 247,240 |
| libm (math) | 13 | 8,774,679 | 72,216 |
| nn::audio | 144 | 3,440,163 | 24,730 |
| nn::util | 25 | 785,195 | 4,200 |
| nn::hid | 82 | 278,291 | 1,962 |
| nn::mem | 11 | 158,321 | 718 |
| nn::fontll | 12 | 26,917 | 0 |
| nn::mii | 102 | 10,047 | 0 |
| nn::fs | 118 | 6,762 | 0 |
| nn::err | 3 | 1,271 | 0 |
| nn::time | 17 | 898 | 6 |
| nn::friends | 11 | 606 | 0 |
| nn::ldn | 1 | 507 | 0 |
| nn::prepo | 26 | 167 | 0 |
| nn::crypto | 18 | 38 | 0 |
| nn::oe | 15 | 20 | 0 |
| nn::lmem | 20 | 17 | 0 |
| nn::account | 10 | 13 | 0 |
| nn::settings | 4 | 8 | 0 |
| nn::vi | 28 | 6 | 0 |
| nn::pl | 3 | 3 | 0 |
| nn::swkbd | 3 | 3 | 0 |
| other C++ (nv) | 2 | 2 | 0 |
| nn::aoc | 7 | 2 | 0 |
| nn::nlibsdk | 22 | 1 | 0 |
| nn::apm | 3 | 1 | 0 |


### What this says

- In a race the game makes about **670,000 SDK calls a second**, and 95% of them are the C library (`memcpy`/`memset`/`strcmp`),
  `nn::os` (`GetTlsValue` alone 165 k/s, mutexes, light events, ticks) and math (`sinf`/`cosf`/`atan2f`). Each one is a dispatcher
  round trip plus recompiled ARM code going through the memory helpers. These are small, well defined functions: the first native work.
- Audio is the next per-frame subsystem (31 k/s, mostly per-voice parameters); hid is small (2 k/s); everything else (fs, mii,
  fontll, friends, account, prepo, crypto, settings, vi, aoc) is load-time or once: replace or stub without performance pressure.
- 1,549 of the 17,270 exports are ever called; 3.0.4 and 4.0.1 call the same set.

## Work list (revised order)

1. **Native C library and math**: `memcpy/memmove/memset/memcmp/strcmp/strlen/strncmp...` and `sinf/cosf/tanf/atan2f/asinf/fmodf...`
   as host hooks working directly on guest memory (fastmem arena: host = base + guest address, checked bounds, fallback to the guest
   code for anything outside). Bit-exact results matter for the game's simulation (same float results as the ARM code: check with a
   table of inputs against the recompiled functions before switching). Measure with `local/exp/ab.sh` (3+ runs each).
2. **nn::os fast paths**: `GetTlsValue/SetTlsValue` (per-thread slot array), `GetSystemTick/ConvertToTimeSpan`; then mutexes, light
   events, reader-writer locks, message queues as host primitives - only where every user of the object is hooked (sdk-internal users
   stay on the emulated kernel until later; never mix).
3. Threads (`nn::os::CreateThread` ...) as host threads without fibers; then hid, settings/oe/time.
4. fs (load time), then audio (largest), then GPU/present cleanup, then the remaining services, then delete the emulator; native
   memory model and function-level codegen last (see the plan).

## Phase 1, step 1: native C library (2026-11-12) - measured, no gain, kept off

`KARTRECOMP_NATIVE_LIBC=1` (`core/arm/recomp/native_libc.{h,cpp}`): `memcpy/__memcpy_device/memmove/__memmove_device/memset/memcmp/
strlen/strcmp` called from the game are answered by host code on guest memory (page by page through `GetPointerSilent`, which also
tells the code guard; any unmapped page falls back to the game's own code). Correct: races fully static, 60 fps, same pictures.
A/B, 1P effects race on 4.0.1, game-thread samples: native 37,898 / 37,881 / 37,325 (mean 37.7 k) vs reference 35,994 / 36,520 /
38,063 (mean 36.9 k): within the noise, no gain. Profile: SDK code 3.3% -> 2.7% of the game thread, host hook dispatch +0.5 points.
The C library was cheap to begin with (the 300 k calls/s are mostly tiny copies); the per-call hook cost eats the saving.

Lesson for the plan: replacing SDK functions one call at a time through the dispatcher hook is a way to *remove emulation*, not a
speed-up; frame-time gains need fewer dispatches (codegen) or less work per access (memory model), as NATIVE_PERF.md found.
Next native steps (nn::os sync/TLS, hid, fs) should be judged by what they remove (kernel, IPC, services, fibers), not by fps.

## Phase 2, step 1: native controller reads (2026-11-12) - works, off by default

`KARTRECOMP_NATIVE_HID=1` (`core/arm/recomp/native_hid.{h,cpp}`): `nn::hid::GetNpadStyleSet` and the five `GetNpadState` overloads
(full key, handheld, Joy-Con dual/left/right) are answered straight from the controller objects (hid_core `EmulatedController`: SDL
pads, keyboard, controller profiles, TAS, the overlay's input block), with the hid service's state layout, button masks and
connection attributes (npad.cpp). The game no longer reads its buttons and sticks through the emulated hid service's shared memory.
Checked: scripted 1P and 4P races (every player's input drives the menus and the race), 60 fps, same pictures as the emulated path.
Step 2 (same day): motion (`GetSixAxisSensorStates` from the controller objects' motion data, "at rest" until the game starts the
sensor, as six_axis.cpp) and touch (`GetTouchScreenStates<1..16>`: one state, no touches; the PC game has no touch screen and nothing
fed the emulated one) are native too: 25 functions in all. The trace confirms every per-frame hid read goes through the native path.
Still on the emulated service: vibration output (the game drives the SDK's software vibration mixer, `VibrationNode::Update`; the SDK
then sends the result to the service from inside the sdk module, which cannot be hooked - it moves with the IPC/kernel work), the
one-time setup calls, and the service's own per-tick update (it still fills shared memory nobody reads).

Direction (user, 2026-11-12): the goal is a true recomp (no emulation left), not speed. Steps are judged by what emulation they
remove. For the OS primitives (`nn::os` locks, events, threads) the plan is a small native kernel at the system-call level rather
than SDK hooks: the SDK's own code uses the same lock words and cannot be hooked, so both sides must go through one implementation.

## IPC: what still goes through the emulated kernel (2026-11-12)

Tool: `KARTRECOMP_IPC_TRACE=1` (`core/hle/service/service.cpp`): every service command, `<log dir>/ipc_trace.txt` every 10 s
(total, last 10 s, "service: command"). 1P race on 4.0.1 with native hid on (`local/exp/runs/ipc_trace-401.txt`, 137 commands):

| per second in the race | service: command | what it is |
|---|---|---|
| 192 + 120 | hid: SendVibrationValues, GetActualVibrationValue | rumble, sent by the SDK's vibration mixer |
| 141 | IAudioRenderer: RequestUpdate | audio renderer frame (voices, mixes, effects) |
| 120 + 120 | nvdrv: Ioctl, Ioctl2 | the game's NVN driver (subsdk0) talking to the GPU service: channel submits, syncpoints |
| 120 | IHOSBinderDriver: TransactParcelAuto | the display queue (dequeue/queue buffer), two per frame |
| 2 + 2 | ISystemClock: GetCurrentTime, ITimeZoneService: ToCalendarTime | the clock |

Everything else (IStorage reads of the RomFS, fs, acc, am, aoc, prepo, mii database, audren setup, sm) happens at boot or loading.

The SDK sends these from inside the sdk module, so they cannot be answered at the SDK API (an internal call reaches the dispatcher
once, then the generated code calls it directly). They are answered at the system-call level instead, which is the native kernel:

## The native kernel (design)

1. **SendSyncRequest without the emulated transport**: the SVC handler decodes the request in the calling thread's TLS command
   buffer and calls a native handler for that service and command synchronously (no KServerSession, no service threads, no
   HLERequestContext). Handlers return plain data or native handles. Start with the per-frame list above (vibration, audio
   RequestUpdate, nvdrv, binder, clock), reusing the existing HLE service code where it is real logic (audio_core's renderer,
   nvdrv's syncpoint/channel code), behind `KARTRECOMP_NATIVE_IPC=<services>`.
2. **Native handle table and objects**: events (readable/writable), threads, shared and transfer memory, sessions to native
   services; `WaitSynchronization`, `ResetSignal`, `CloseHandle` on them.
3. **Native threads**: one host thread per guest thread running the dispatcher (no fibers, no emulated scheduler); `CreateThread`,
   `StartThread`, `ExitThread`, `SleepThread`, priorities as host hints, `GetCurrentProcessorNumber`.
4. **Native synchronization**: `ArbitrateLock/Unlock`, `WaitProcessWideKeyAtomic/SignalProcessWideKey`, `WaitForAddress/
   SignalToAddress` as futexes on the guest words (the SDK's own lock words, so the SDK and the game share one implementation).
5. **Memory SVCs** (`SetHeapSize`, `MapMemory`, `QueryMemory`, `SetMemoryAttribute`, ...) on our own address-space map.
6. Then the HLE kernel, service manager and `core/hle/service/*` are no longer used and go.

Steps 1-2 can run on top of the emulated kernel (native services for some commands, the rest unchanged); steps 3-5 switch the
whole process over at once (one kernel or the other), so they come after every service the game uses has a native handler.

## Native kernel step 1: IPC on the calling thread (2026-11-12) - works, off by default

`KARTRECOMP_NATIVE_IPC=all-safe` (or a comma list of service names; `core/hle/service/native_ipc.{h,cpp}`, hooked into
`SendSyncRequest` in `core/hle/kernel/svc/svc_ipc.cpp`, sessions registered from `ServerManager::RegisterSession/DestroySession`):
a request to an enabled service is handled on the thread that sent it - the same handler and reply, but no request queue, no
service thread, no wake-ups. Works for plain sessions and domains (the target object's handler is found from the domain header).
Only plain requests (CMIF types 4 and 6); Close and control messages keep the emulated path.

Safe services (they never defer a reply - only `sm` does - and never wait on the host; every request still runs under the
service's own lock): `ISystemClock`, `ITimeZoneService`, `ISteadyClock`, `hid` (incl. rumble), `IActiveVibrationDeviceList`,
`IAudioRenderer`, `nvdrv`. Not yet: the display queue (`IHOSBinderDriver`: `DequeueBuffer` waits for a free buffer, which on the
calling thread would block a whole emulated CPU core) - it moves with native threads.

Checked: 1P and 4P races on 4.0.1 (with native hid): all six services seen on the native path in the log, 60 fps, the same 12 log
errors as the emulated path. To hear-test: music, sound effects and rumble should be unchanged.
With this, the per-frame IPC left on the emulated transport is the display queue (~120/s) and load-time requests.

## Transferable by design: `src/kart_runtime/` (user rule, 2026-11-12)

The end state is our own engine, so new native parts must not be written against suyu's classes. They go into the fork's
`src/kart_runtime/`: standard library only (SDL where a device is needed), nothing from suyu included or named, own tests that
build without the emulator (`src/kart_runtime/tests/build_and_run.sh`). suyu reaches them through thin adapters in its own files;
when suyu goes, the adapters go and the runtime stays. The earlier suyu-bound pieces (`native_hid`, `native_ipc`, `native_libc`)
get the same treatment when they are next touched: logic behind a runtime interface, suyu glue outside.

### First component: `kart_runtime/core_scheduler.h` (native threads, part 1)

Host threads sharing virtual CPU cores the way the console's threads do: one running thread per core, the best priority always
first, FIFO among equals, and a preemption *request* the running thread honours at a safe point (`Acquire`, `Release`, `Yield`,
`PreemptRequested`, `SetPriority`, `SetCore`). Games rely on these rules (threads of one core share data without locks), so 1:1
host threads need them; fibers and the four emulated-core threads provide them today.
Tests (`tests/test_core_scheduler.cpp`): 24 threads on 3 cores never overlap on a core, waiters are served by priority then
arrival, preemption request and yield, priority and core changes. Pass, also under ThreadSanitizer.

### Part 2 (next): the adapter - game threads as host threads in suyu

What suyu already has: its service threads are host threads registered as "dummy" KThreads; when the kernel makes one wait, it
blocks on a host condition variable after the scheduler lock is released (`KScheduler::RescheduleCurrentHLEThread` ->
`KThread::DummyThreadBeginWait`), and it is woken when its state becomes runnable (`OnThreadStateChanged` ->
`GlobalSchedulerContext::WakeupWaitingDummyThreads` -> `DummyThreadEndWait`). Game threads can use the same path. To do
(`KARTRECOMP_NATIVE_THREADS=1`, all at once - one kernel mode or the other):
1. A game thread starts a host thread instead of a fiber (`KThread::InitializeUserThread`, `CpuManager::GetGuestThreadFunc`); the
   host thread sets itself as the kernel's current thread and runs the dispatcher loop (`MultiCoreRunGuestThread`'s body).
2. Each such thread needs its own CPU interface (`ArmRecomp` holds the registers per emulated core today: `PhysicalCore::RunThread`,
   `KProcess::GetArmInterface(core)`); `GetCurrentProcessorNumber` answers the thread's virtual core.
3. Kept out of the emulated cores' priority queue (`KScheduler::OnThreadStateChanged`, `UpdateHighestPriorityThreadsImpl`); the
   four `CPUCore_N` threads and their idle threads are not started.
4. `CoreScheduler::Acquire` before running game code, `Release` when the kernel makes the thread wait (in the dummy-wait path),
   `PreemptRequested` checked where `PhysicalCore::IsInterrupted` is checked now, then `Yield`; `SleepThread(0/-1/-2)` -> `Yield`;
   priority and core-mask SVCs -> `SetPriority` / `SetCore`.
5. Termination and suspension (`KThread::RequestTerminate`, process pause) reach the host thread through the same interrupt check.
Then the display queue can join `KARTRECOMP_NATIVE_IPC` (a blocking wait only blocks its own thread), and the kernel's remaining
pieces (handle table, synchronisation objects, memory map) are moved into `kart_runtime` one by one.

## Native threads, part 2: done (2026-11-12) - `KARTRECOMP_NATIVE_THREADS=1`, off by default

`core/hle/kernel/native_threads.{h,cpp}` (the suyu adapter) + `kart_runtime/core_scheduler.h`. Every game thread and every service
thread that was a fiber on an emulated core (`KThread::InitializeUserThread` / `InitializeServiceThread`) is a host thread
("KartThread" / "KartService"); the four `CPUCore_N` threads are not started and no fibers are created.
- Start: the first time a thread becomes runnable (`KScheduler::OnThreadStateChanged`) its host thread is spawned.
- Running: the host thread takes its virtual core from `CoreScheduler` and uses that core's `PhysicalCore` and CPU interface
  (registers loaded/saved with `LoadContext/SaveContext`), so everything keyed by core index works unchanged
  (`KernelCore::CurrentPhysicalCoreIndex` answers the held core).
- Waiting: the kernel's dummy-thread path (`KThread::DummyThreadBeginWait`): save registers, release the core, block on the host,
  then take a core again. A thread suspended by another thread waits the same way (it is interrupted first).
- Not in the emulated cores' priority queue; priority and core changes go to `CoreScheduler`; `SleepThread(0/-1/-2)` = `Yield`;
  a better-priority waiter interrupts the running thread (`SetPreemptCallback` -> `PhysicalCore::Interrupt`).
- Exit: the host thread releases its core and parks (thread objects are recycled, so it must not touch its KThread again - the
  first version parked on the object's own wait variable and froze the game at the first race load). TODO: end the host thread.

Checked (4.0.1): 1P and 4P races, alone and together with native hid + native IPC: 59.9 / 59.5-59.6 fps, the same 12 log errors
as the emulated path, fully static, clean exit. CPU: 79.8 k samples on game threads vs ~79 k on the emulated cores (1P), 104 k vs
106 k (4P): the same within noise. Default path (switch off) unchanged. Not yet checked: 3.0.4, mods, long sessions, Windows.
Known gaps: no core migration for threads allowed on several cores (the console moves a waiting thread to an idle core), no
round-robin time slice for priority 59; debug switch `KARTRECOMP_ALLOW_PTRACE=1` lets gdb attach to a test run.

Next: the display queue onto `KARTRECOMP_NATIVE_IPC` (its blocking wait now only stalls one virtual core), then moving kernel
pieces (handle table, synchronisation objects, memory map) into `kart_runtime`, and the play test before any of this is default.

## First play test (user, 2026-11-12) and what it showed

Mario Circuit with the user's mods and settings (FSR 3 100%, HDR, 3x frame generation, vsync, 1440p), all native switches on:
"a lot of slowdown", audio volumes "a little mixed up", delay "a little off", then a crash after ~5 minutes.
- **Slowdown**: the user's `launch.env` pins the emulated-core threads (`KARTRECOMP_PIN=2,3,4,5`); with native threads those threads
  do not exist, so nothing was pinned and every hand-off woke a host thread on another CPU. Native threads now honour
  `KARTRECOMP_PIN` (virtual core N's threads run on the Nth CPU). Scripted race with the user's settings and mods: emulated+pin
  59.58 fps / 10 s under 58; native without pin 59.24 / 15 (min 49); native+pin 59.48 / 11.
- **Audio**: recorded the real output (`pw-record` of the sink) for emulated / threads / IPC / both in a scripted run: same level
  (x0.9-1.05 per 10 s), menu envelope correlation 0.93-0.99, offsets = start-time differences. No measurable difference; the heard
  mix/delay is most likely the slowdown (picture slow, audio real time). To confirm by ear after the pin fix.
- **Crash**: not native-related: the known character-mod null call (docs/NATIVE_GPU.md "PC 0"); the engine's null-call recovery
  stopped after 100,000 calls (~5 min of a race with that racer). The cap is removed.

## Fast code: direct memory, whole-image guard, chained branches, native calls (2026-11-12)

The user's slowdown (Mario Circuit, bursts of 17-25 ms frames once per lap) is the main game thread running out of CPU: in a
scripted three-lap 50cc race without mods it already averages 64% busy and peaks at 81%. Four changes to the generated code cut
that. All are static (no JIT), and Setup builds them on Linux by default (`BuildRunner::FastCode()`;
`KARTRECOMP_NO_FAST_CODE=1` at Setup time builds the old form; Windows keeps the old form until its engine has the fault handler).

Measured on 4.0.1, Mario Circuit, 3 laps, native threads, pinned, 180 s profile; first only the 127 hot units of `main` rebuilt:

| build | main game thread samples | all game threads | vs before |
|---|---|---|---|
| before | 115,339 | 249,303 | |
| direct memory | 96,982 | 208,875 | -16% |
| + conditional branches chained | 93,694 | 198,335 | -20% |
| + native calls (BL and BLR) | 91,660 | 193,318 | -22% |
| + whole-image guard (no per-block check) | 83,757 | 179,769 | -28% |

1. **Direct memory (image feature DM1, `-DRECOMP_DIRECT_MEM=1`).** Every load and store used to call a helper that looked the
   address up in the page table (two dependent loads, a call, and the compiler had to assume the call changed every register in
   the context). Now game address A is read at `arena + A`, inline: the arena is the engine's existing reservation of the whole
   39-bit address space (suyu's "fastmem arena", `HostMemory::VirtualBasePointer`), mapped with the game's own permissions. Each
   block reads the arena base once (`_dm`, declared by `RECOMP_GG_GUARD`) and takes its context as a `restrict` pointer, so a
   store to game memory does not make the compiler reload registers. The module gets the arena through
   `recomp_image_direct_v1` (engine: `ActivateDirectMem` in `arm_recomp.cpp`, before the first guest block runs).
   What the lookup did for bad addresses is now `src/kart_runtime/direct_memory.h` (standalone, with a test): a fault handler
   that makes reads of unmapped memory return zeros (a zero page is mapped there, so a mod that leaves a null object in the
   game, like the Trump driver on Donkey Kong, faults once per page instead of taking the slow path 37 times a frame), drops
   writes to unmapped memory (single step), and opens mapped pages the game has no permission for (the lookup ignored
   permissions). The counts are logged with the frame pacing line (`direct memory: ...`).
   Limits: an address outside the 39-bit space is not caught (it was read as zero before; now it is a host access relative to
   the arena, in practice a crash); the arena maps 4 KB pages, so the earlier huge-page gain of the backing mapping (2%) is
   not available on this path yet.
2. **Whole-image guard (GW1, `-DRECOMP_GUARD_WHOLE=1`).** Every block used to compare a per-block "seen" word with the
   module's generation word on entry (three loads, one of them a cache line per block). Instead each module exports
   `recomp_image_verify_code_v1`: an FNV-1a hash over all instruction words of the span its blocks cover, checked once against
   the game's memory before any guest code runs (`VerifyWholeImages`). A mismatch (other update, a code-patching mod or cheat)
   stops the engine with a message that says to run Setup again; before, each block noticed for itself.
3. **Conditional branches chain (exporter `--chain-cond`).** B.cond, CBZ/CBNZ and TBZ/TBNZ tail-call their targets like B
   does, instead of returning to `recomp_image_run_slice`, whose 23 MB block index misses the cache for most targets.
4. **Native calls (exporter `--native-calls`, now also for BLR).** BL and BLR call the target block as a C function; a RET to
   the instruction after the call continues there without a dispatcher round trip.

Tried and dropped: checking only the chain budget at block exits (not `halted` and `pending_svc`): the game hung at boot.

Still open on this path: the per-block entry check (`c->pc` against the block's own address) and the `c->pc` store on every
chained exit; huge pages for the arena; Windows (needs a vectored exception handler doing what `direct_memory.h` does).

## "Decrypted files" Setup mode (2026-11-13)

Setup can take the game as already decrypted files instead of NSPs: a code folder (ExeFS: `main`, `main.npdm`, `rtld`, `sdk`,
`subsdk0`, update 3.0.4 or 4.0.1 applied; the update is recognised by `main`'s build id) and the data (a RomFS folder, or a
`romfs.bin` image). Nothing is copied: `content/decrypted.txt` in the data folder names the two paths.

- Exporter: `kartrecomp-export --exefs <folder> --out <dir>` (no keys). Same output as from the NSPs (checked: identical files).
- Engine: started with `<exefs>/main` as the game, `KARTRECOMP_DECRYPTED=1` (no keys/firmware check, the folder loader is tried
  first so no container probe decrypts anything) and `KARTRECOMP_ROMFS_DIR=<folder or image>` (`FileSys::CreateRomFS` lays the
  image out over the folder's files).
- Launcher: mode box on the Setup page, `DataLayout::contentArgs()` / `addContentEnv()` for every launch and the test boot,
  `kartrecomp-setup --exefs <dir> --romfs <dir|file>`. DLC (NSPs) is not available in this mode.
- Verified: whole pipeline in a sandbox with no keys or firmware, 4 min 53 s, test boot passed; title screen reached from both a
  `romfs.bin` and a folder.
- **Not solved: no firmware means no Mii data, and the game then destroys its own memory.** suyu's stand-in for the MiiModel
  system archive is six 16-byte files. The game treats them as real, ends up with a vertex buffer at address 0x1903c with a size of
  about 4 GB, and converts it in place (`main`+0x8e408c calling 0x8e53f0, 4.0.1): four billion stray writes that run over the
  game's own code and data. With the slow page-table path this took minutes; with direct memory it takes seconds, and the game
  crashes about 25 s after start. Without the archive at all the game does not boot; a 64 KB all-zero stand-in fails differently.
  A guard at the loop entry also broke normal boots. So for now the mode has optional keys + firmware rows, and with them it is
  clean. A real fix needs a stand-in archive in the right format (NFTR/NFSR, the Switch form of the FFL resource: header with
  per-part maximum sizes, then part tables) with at least empty parts the game accepts.
- `kart_runtime/direct_memory.h` gained sink ranges for such runaway writers (a page that takes 16 dropped writes stays
  writable and grows ahead of the writer; `ZeroSinks()` empties them once per frame), and the fault report names the writing
  code (`latest dropped write from <module>+<offset>`). Research aids: `KARTRECOMP_SYSARCHIVE_INFO=1` (files and headers of
  the system archives the game opens), `KARTRECOMP_ALLOW_PROBE=1` with `--content-probe --content-dump <dir>` (writes exefs and
  romfs.bin from the NSPs).
