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
- **Solved on 2026-11-13 (see "Stand-in Mii resources" below); what was wrong:** no firmware means no Mii data, and the game then destroyed its own memory. suyu's stand-in for the MiiModel
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

## Memory: zero punch (2026-11-13), and two things that did not pay

- **Zero punch (on by default on Linux, `KARTRECOMP_ZERO_PUNCH=0` off).** The game clears large heaps with `memset(…, 0, …)` and
  never touches most of them: in a race 865 MB of the 1696 MB of game memory were pages holding only zeros. The engine now takes
  over `memset` calls from `main` into the sdk when they fill at least 256 KB with zero, and releases the whole pages inside the
  range (`MADV_REMOVE` on the memory file; they read as zeros and get a page again when written) instead of writing them
  (`core/arm/recomp/native_libc.cpp`). Measured (effects race, native threads): resident memory 3.5 GB -> 2.5 GB, game memory
  allocated 1696 -> 901 MB, CPU unchanged (82.5k main-thread samples against 81.6-83.6k), 4-player, 3.0.4 and a mods run clean.
- **Cold code at -O1 instead of -O0** (`KARTRECOMP_COLD_OPT=-O1` at build time): modules 1.3 GB -> 0.7 GB on disk, resident
  memory only -70 MB, CPU unchanged, build 9 min instead of 4. Left off.
- **Two functions per block** (exporter `--fast-entry`: a body entered at instruction 0 by chained edges, and an index function
  that works out the entry from the pc; no entry check and no pc store on chained edges): 80.9k and 82.8k main-thread samples
  against 81.6-83.6k without, so no gain beyond run-to-run noise (about 3%), and modules 9% bigger. The option stays in the
  exporter, the launcher does not use it.
- `KARTRECOMP_MENU_SPEED=<percent>` (test switch): the game runs at that speed whenever a frame has no game camera (menus) and at
  the normal speed in races. Made for a "do the menus look too fast" comparison; the user's menus turned out to be right.

## Stand-in Mii resources: the game runs with no firmware (2026-11-13)

`core/file_sys/system_archive/mii_model.cpp` now builds the six MiiModel files in their real layout with no parts in them:

- texture files ("NFTR", four of them): magic, version 1, file size, 11 maximum part sizes, 11 alignments (0x200), padding to
  0x160, then 318 part entries of 28 bytes (data offset, size, compressed size, zlib parameters, alignment, width/height, format
  and mip count); data would start at 0x2428.
- shape files ("NFSR", two): magic, version 1, file size, 12 maximum part sizes, 12 alignments (4), padding to 0xc4, then part
  entries of 88 bytes and transform tables up to 0x14dd4.
- An entry that is all zero is a part that does not exist (the real files have 115 such texture entries and about 300 shape
  ones), so a file whose tables are all zero is valid and simply has no parts. The layout was read from a dump of the user's own
  firmware (`KARTRECOMP_SYSARCHIVE_DUMP`); no data from it is in the stand-in.

Result: the keyless "decrypted files" boot (no keys, no firmware, RomFS folder) runs menus and a scripted Grand Prix race for
110 s at 60 fps with no stray memory accesses at all (before: 300,000 dropped writes a second and a crash after 25 s). Miis have
no faces or bodies in this mode. Installs with firmware are unaffected: the stand-in is only used when the archive is missing.

## Native paths are the default (2026-11-13)

On Linux the engine now starts with native threads, native controller reads and native IPC ("all-safe") without any switch, and
the native renderer is the only one that draws on every platform. `KARTRECOMP_NATIVE_THREADS=0`, `KARTRECOMP_NATIVE_HID=0`,
`KARTRECOMP_NATIVE_IPC=0` turn each off; elsewhere they stay opt-in (`=1`) until the Windows build has been tested with them.
`KARTRECOMP_NATIVE_GPU=shadow` is the development comparison mode, `=off` is for tools (the launcher's test boot and the
firmware reader below); there is no emulated-GPU picture any more (the old renderer runs as Null whenever the native one is not
in shadow mode). Mario Circuit, 4.0.1, defaults only: 59.9 fps, no second under 58, game threads as host threads.

What is still the old console layer underneath: the HLE kernel objects (handles, memory map, synchronisation), the services the
game talks to through IPC (file system, audio renderer, the display queue, account, settings) and the loader. They are called
on the game's own host threads now, but the code is suyu's. Removing them is the remaining part of the plan above (native
handle table and synchronisation, then file system and audio).

## Tuning points and ultrawide (2026-11-14)

Game changes at run time without JIT and without patching code: a **tuning point** is an instruction whose float result the
generated code multiplies by a number the engine can set. `common/kart_tune_sites.h` lists them (game build, address, expected
instruction, register, slot); the exporter marks the ones whose instruction matches, the generator appends
`c->vreg[r] *= recomp_tune[slot]` after that instruction, and the module exports the table (`recomp_image_tune_v1`). All slots
start at 1.0, so a game with tuning points and an engine that never sets them behaves exactly as before, and an older game
build simply has none (`KartTune::Set` returns false). `export.json` records `tune_sites`.

**Slot 0, view aspect.** The game sets each camera's aspect to its view's width / height in four places (one `fdiv` each,
stored at +0xb0 of the engine library's perspective projection; found by scanning a running race for the 16:9 value, writing
21:9 there and seeing the picture widen, then searching `main` for the divide-and-store): 4.0.1 `main+0x3ccab4`, `+0x3cd258`,
`+0x3cd314`, `+0x3ce048`; 3.0.4 `+0x3ca93c`, `+0x3cb0e0`, `+0x3cb19c`, `+0x3cbecc`. Because the game's own projection changes,
its culling follows: nothing pops in at the sides.

**Renderer** (`KARTRECOMP_ASPECT` = `auto` for the window's shape, `W:H`, or a number; launcher: Display, "Picture shape"):
- `wide_x` = wanted aspect / (16:9), set once; the tuning slot gets the same value.
- In a race (the game camera was seen within the last 30 frames) every screen-size render target is `wide_x` times wider
  (`Image::wide`, beside the vertical `scale`), and viewports and scissors scale horizontally by `scale * wide`.
- The HUD is a 16:9 layout: after the upscale point (where the HUD starts) a draw keeps its 16:9 shape in the middle of the
  wide target, unless it reads a wide image (a full-screen copy or effect, e.g. the final copy to the window image) or reads no
  texture at all (a fade or the pause dimmer): those fill the target.
- Menus, their 3D previews and loading screens are drawn 16:9 exactly as before (their cameras are fixed 16:9) and shown in
  the middle with black bars; the targets change size at the switch.
- Checked on 4.0.1 at 21:9 (3360x1440): menus, vehicle and cup select, the pause menu and a race look right, 60 fps.
  Not checked yet: split-screen, replays, the award ceremony, 32:9, frame generation and HDR together with ultrawide, handheld
  quality, effects that work in screen pixels (none seen wrong so far).

## Mii preview in the launcher (2026-11-13)

`app/src/mii_render.{h,cpp}`: the Miis tab draws the Mii's head and shoulders and redraws on every change. It is a firmware
feature: the face data is the console's own "MiiModel" archive, read from the player's installed firmware by the engine
(`KARTRECOMP_SYSARCHIVE_EXTRACT=0100000000000802=<folder>`, exits before any game code and needs no built game) into the
launcher's cache folder, once; the eye, mouth and makeup textures are ASTC and are decoded by the engine too
(`mk8d-engine --astc-decode <in> <out>`). Without firmware the Miis tab is off (the game has no Mii racers then either).
Nothing from the firmware is in the repository or a release.
- Resource layout, placement formulas and colour tables follow the MIT/Apache "vee" Mii library; block-linear unswizzle, BC4 and
  BC5 decoding and the drawing (a software rasterizer with depth buffer, bilinear textures, alpha layers, 2x supersampling)
  are the launcher's own. No GPU or OpenGL is used.
- Face texture: skin colour, makeup, wrinkles, painted beard (the texture is one half of the face, mirrored). Mask: mustache,
  mouth, eyebrows, eyes, mole. Meshes: face, forehead, hair (mirrored when parted the other way), nose, nose line, beard,
  glasses. The shoulders are a plain bust in the favourite colour (the firmware has no body; games bring their own).
- Development: `kartrecomp --mii-render <resource folder> <out.png> [key=value ...]`, and
  `KARTRECOMP_LAUNCHER_SHOT=<tab>=<png>[=<seconds>]` saves a picture of the launcher window.

## Frame rate (2026-11-14)

The game's simulation is 60 steps a second and counts frames (docs/FRAMERATE.md), so the launcher's Frame rate tab offers what
is real: 60, or 120 / 180 / 240 with frames generated by the renderer between the game's own (the built-in interpolator with the
game's motion vectors; it switches FSR 3 on at 100% when no temporal upscaler is chosen), and it points to the Setup page's
60 fps split-screen build option for 3-4 players. A game rate above 60 would need every frame-counted timer in the game
rewritten; that has not been done.

## Game build time (2026-11-14)

Measured on the 4.0.1 build (clang 20, 6 jobs on 4 cores / 8 threads, 236 s): 160 s are the "hot" pieces (the 176,577 blocks
of `hot_blocks.txt`, 35% of the generated text, compiled at -O3), 60 s the cold ones (-O0), linking about 2 s, the export 2 s.
A hot piece costs about 1.2 s per MB at any level from -O1 to -O3 (-O0: 0.2 s), half optimiser and half code generation: the
cost is in the shape of the generated code (every instruction is a possible entry), not in one pass. What changed:

- **Incremental builds.** Every compile job has a key: a hash of its source text, the module's headers, the compile options
  (without install-specific paths) and the compiler. The object's marker stores it, and a piece is compiled again only when
  the key differs. The work folder is no longer wiped when the engine version changes. A second build of an unchanged export:
  13 s instead of 237 s (hashing, splitting and linking). A generator change that touches a few blocks (a new tuning point, a
  code patch) recompiles only the pieces holding them; a change to the runtime header still recompiles everything.
- **Hot pieces of about 1.5 MB** (were one per unit, up to 20 MB and 72 s for a single file): 4% less build time, no long
  single compile at the end, and finer pieces for the incremental build. CPU in the race unchanged within noise.
- Not done, with reasons: a smaller hot list (the list is every block that ran in the coverage runs; cutting it to the
  blocks of one profile risks -O0 code in whatever the profile did not visit), -O1 for the hot pieces (9% faster to compile,
  bigger code).

## Native kernel step 2: locks, condition variables, address waits, sleeps, display queue (2026-11-14)

On by default on Linux together with native threads (`KARTRECOMP_NATIVE_SYNC=0` turns it off; elsewhere opt-in with `=1`, and
never without native threads).
- `kart_runtime/sync_arbiter.h` (standalone, test `tests/test_sync_arbiter.cpp`, also clean under ThreadSanitizer) is what the
  console kernel does for `ArbitrateLock/ArbitrateUnlock` (0x1A/0x1B), `WaitProcessWideKeyAtomic/SignalProcessWideKey`
  (0x1C/0x1D), `WaitForAddress/SignalToAddress` (0x34/0x35, all three wait and all three signal kinds) and `SleepThread` with a
  time (0x0B): the same rules and results (hand-over of the lock word with the wait bit, the condition key written before the
  lock is released, best priority first, the "modify by waiting count" arithmetic, timeouts), with waiting threads blocked on
  host condition variables and their virtual core given up meanwhile. The words are changed with host atomics in the game's
  memory, which the game's own exclusive loads/stores see. A thread being terminated has its wait ended
  (`OnTerminateRequested`, from `KThread::RequestTerminate`), so closing the game does not hang.
- Adapter `core/hle/kernel/native_sync.{h,cpp}`: called from the five system calls before the emulated kernel's
  `KConditionVariable` / `KAddressArbiter`. These carried 2.26 million of the 2.5 million system calls of a 150 s race; they no
  longer touch the kernel's thread queues, scheduler lock or timers. Service threads (no game process) only sleep through it.
- **Display queue** (`IHOSBinderDriver`) joins the calling-thread IPC list when native threads are on: a game thread that waits
  for a free buffer gives its core up (`NativeThreads::BeginHostWait/EndHostWait` around the wait in `BufferQueueCore`, the core
  taken back without the queue's lock held).
- Checked: 1P race 4.0.1 and 3.0.4 (60 fps, same pictures, lap 2-3 reached), 4P split-screen 4.0.1 (59.8 fps, 0-2 seconds under
  58), clean exit in under a second. The CPU is the same as before within noise (the kernel path was already cheap: HLE
  kernel/services were 0.3% of the game threads).

What is still the emulated kernel: `WaitSynchronization` / `ResetSignal` / `CancelSynchronization` on events (the game waits on
service events: vsync, audio, GPU syncpoints), the IPC transport for the services that are answered on the calling thread, the
handle table, the memory map system calls and thread creation bookkeeping (the threads themselves are host threads).

## Native graphics queue: the emulated GPU gets no work (2026-11-14)

On by default on Linux with the native renderer (`KARTRECOMP_NATIVE_QUEUE=0` turns it off; elsewhere opt-in with `=1`).
`core/nvn_host/nvn_host.cpp` answers the game's NVN queue, sync and window calls itself, so the game's GPU driver never
submits a command stream: `nvnQueueSubmitCommands` (the native renderer takes the submission, as before), `nvnQueueFlush`,
`nvnQueueFinish`, `nvnQueueFenceSync`, `nvnQueueWaitSync`, `nvnSyncWait` (done at once: the emulated GPU's Null renderer also
signalled as soon as it had read the commands), `nvnWindowAcquireTexture` (the window's textures in turn) and
`nvnQueuePresentTexture`.
- **Counter reports** (`nvnCommandBufferReportCounter`, ~6 a frame: timestamp, samples passed and type 0x10): recorded as a
  command and written when the submission is taken, as the GPU wrote them for this game: value 0, then the GPU timer
  (measured first with `KARTRECOMP_NVN_COUNTER_PROBE=1`: every report the game reads held 0).
- **Pacing**: the display queue's blocking `DequeueBuffer` used to hold the game to the refresh rate (without it the game ran
  at ~870 fps). A display clock now does the same: each present is shown one refresh after the previous one (or at once when
  the game is late; `KARTRECOMP_VSYNC_HZ` as before), and an acquire waits, with the game thread's core given up, while more
  than (textures - 2) presented frames are still waiting to be shown (the window has 3).
- The window's per-frame work (input scripts, overlay requests) ran when the emulated display showed a frame; it now runs on
  each native present (`NvnHost::SetFrameDisplayed`), and the fps counter counts native presents.
- Checked: 1P 4.0.1 and 3.0.4, 4P split-screen 4.0.1 with the user's display settings (frame generation): 60 / 59.9 fps, same
  pictures, laps reached. The emulated GPU thread no longer appears in a CPU profile at all (it was 0.35%).

Not done yet: the emulated GPU is still created (about 40 MB) because the game's driver still uses the GPU service for channel
setup and GPU address-space mappings at boot (`nvhost_as_gpu`, `nvhost_gpu`); those need native answers before `Tegra::GPU`
can go.

## Native file system: the game's content on the host (2026-11-14)

On by default on Linux (`KARTRECOMP_NATIVE_FS=0` turns it off; elsewhere opt-in with `=1`).
- `kart_runtime/native_fs.h` (standalone, test `tests/test_native_fs.cpp`): the SDK's rules for content files - `<mount>:/path`
  names (`//`, `.`, `..`), file and directory handles (tagged values that cannot be game heap addresses, so calls on other
  handles go to the SDK), the result codes (path not found 0x202, out of range 0x177202, ...), reads with and without a size
  result, entry types, directory listings (subdirectories first, then files, 0x310-byte `DirectoryEntry`), mode checks.
- Adapter `core/arm/recomp/native_fs.{h,cpp}` hooks 14 `nn::fs` functions of the sdk module (`MountRom`,
  `QueryMountRomCacheSize`, `Unmount`, `OpenFile`, `CloseFile`, `GetFileSize`, four `ReadFile` forms, `GetEntryType`,
  `OpenDirectory`, `ReadDirectory`, `CloseDirectory`). The tree is the game's RomFS exactly as the file system service hands
  it out (`RomFsController::OpenRomFSCurrentProcess`: update, mods and text patches applied), read with suyu's RomFS reader.
- The game mounts its content as `content:`. Storage reads through the file system service in a 150 s race: 1,907 -> 18 (the
  rest are the system archives: fonts and Mii data). Save data, add-on content and system data keep the SDK path.
- Checked: 1P races on 4.0.1 and 3.0.4 (60 fps, same pictures), the keyless "decrypted files" boot. Not checked: a run with
  mods (they come through the same RomFS image, so they should apply the same way).

## Native services (2026-11-14)

On by default on Linux (`KARTRECOMP_NATIVE_SERVICES=0` turns it off, or a comma list of service names picks some; elsewhere
opt-in). The services a game opens through the service manager are answered by the runtime instead of the emulated kernel's
sessions and suyu's HLE service code.
- `kart_runtime/hipc.h`: the service message format (HIPC transport + CMIF commands: handle descriptor and process id, X/A/B/W/C
  buffer descriptors, the 16-byte aligned raw data with "SFCI"/"SFCO", domain headers with object ids, control and close
  messages), read and written as the SDK lays them out.
- `kart_runtime/native_kernel.h`: handles (bit 30 set, so they cannot collide with the emulated kernel's), sessions and domains
  (ConvertToDomain, Clone, QueryPointerBufferSize, object close), services as command tables answered on the calling thread,
  events and WaitSynchronization on them (wait any, timeout, CancelSynchronization, termination). Test
  `tests/test_native_kernel.cpp` builds requests as the SDK does and reads replies as the SDK does; clean under ASan/UBSan/TSan.
- Adapter `core/hle/kernel/native_services.{h,cpp}`: `sm`'s GetService hands out a runtime session for a runtime service; the
  system calls SendSyncRequest, CloseHandle, ResetSignal, WaitSynchronization and CancelSynchronization take runtime handles
  first; thread termination ends runtime waits. `native_services_list.cpp` builds each service with the engine's settings
  (language, region, the user profiles - their ids name the save data -, the DLC found among the player's files).
- Services (`kart_runtime/services/`): clock (`time:u/a/s`: host wall clock, host time zone, steady clock since start),
  `set` (language list and code, region), `apm` (performance sessions), `pctl`, `pctl:a/r/s` (no restrictions), `prepo:*` (play
  reports accepted and dropped), `friend:*`, `lm` (game log dropped), `mm:u`, `acc:u0/u1` (profiles, account manager),
  `aoc:u` (add-on content list and its event).
- Found on the way: a close message must not remove the client's handle (the SDK closes it afterwards and stops the game when
  that fails).
- Checked: 1P race on 4.0.1 with 13 services on the runtime, 60 fps, the same 12 log errors as before.
- **Applet manager** (`appletOE`, `kart_runtime/services/applet.h`): lifecycle messages behind one event (the focus message waits
  at start, operation / performance mode changes from the engine's graphics-quality switch via
  `AppletManager::OperationModeChanged`, exit), launch parameter (preselected user), desired language and display version (the
  engine's rules from the game's control data), the game's display layer (still made in the emulated display service), exit lock,
  terminate result (logged), and library applets: the controller applet answered at once with the engine's controller
  reconfiguration (what the emulator's default controller applet did), the web applet (offline manual pages extracted into the
  cache folder and opened in the system browser, web addresses opened directly; the frontend sets the opener with
  `SetOpenUrlHandler`), any other applet finished at once with no output.
- A thread whose runtime wait ends because it is being terminated now exits inside the wait (`KThread::Exit`), as the emulated
  kernel's run loop does: returning the error to the game made the SDK stop with a panic at shutdown.
- After a library applet's output is collected the game gets a "resume" message when it turned those on
  (SetRestartMessageEnabled): without it MK8 kept players 2-4 frozen on the character screen after the controller applet.
- **Display service** (`vi:m/u/s`, `kart_runtime/services/display.h`): displays, the game's layer (the applet manager makes it
  here when the display service is native), the stray layer the game opens, the native-window parcel, and the buffer queue's
  setup transactions (Connect, SetPreallocatedBuffer, RequestBuffer, Query, ...) with Android parcels. Frames never go through
  it (native graphics queue). The vsync event is signalled by a once-per-refresh host tick (`KARTRECOMP_VSYNC_HZ`).
- Checked: 1P race (60 fps, pictures), 4P split-screen through the controller applet (59.8 fps, all four players racing),
  clean exits. Test scripts: `local/exp/mprun3.sh` now runs without mods too (`KARTRECOMP_NO_MODS=1`); its "4 views" summary
  only says the menus were split, check the shots.
- **File system service** (`fsp-srv`, `kart_runtime/services/filesystem.h`; engine side `core/hle/kernel/native_fs_backend.cpp`):
  save data (IFileSystem / IFile / IDirectory) and system archives (IStorage) with the service's argument layouts and results;
  the storage underneath is suyu's file-system library (save data factory, NCA/RomFS readers, synthesized archives), the same
  objects the emulated service used. Like the emulated service: Commit answers success (writes reach the save folder directly),
  storage reads past the end return what is there. The runtime fills in the sender's process id (the kernel's job), which
  `SetCurrentProcess` needs. Checked: 1P race, saves byte-identical before/after (this race writes none).
- **Shared fonts** (`pl:u`): the font table of the engine's font loader (firmware fonts or stand-ins); the fonts' shared
  memory is still the emulated kernel's object (memory system calls are emulated), handed out as a copy handle.
  **Mii database** (`mii:e`, `mii:u`, `core/hle/kernel/native_mii.cpp`): the service around suyu's Mii library.
- **Controllers** (`hid`, `core/hle/kernel/native_hid_service.cpp`): the service around hid_core's resource manager (the
  input library: supported styles and pads, Joy-Con assignment, motion, rumble, the shared memory the SDK reads, the style-set
  update events), about 35 commands. The "applet resource user id" argument is the sender's process id from the kernel, as the
  emulated service read it (taking the message's value made the shared-memory lookup fail). Checked: 1P and 4P races (all
  four players racing, 59.9 fps), back to the 12 baseline log errors and no shutdown panic.
- **Audio** (`audren:u`, `core/hle/kernel/native_audio_service.cpp`): the service around audio_core (renderer manager,
  renderer sessions with the game's transfer memory and process taken from its own handle table, audio device names and
  volume); RequestUpdate copies the game's input, output and performance buffers. The renderer's "rendered" event is still an
  emulated kernel event signalled by audio_core. At engine shutdown the runtime stops its renderers before audio_core goes
  (`NativeServices::Shutdown`, from `ShutdownMainProcess`), and late requests answer "invalid state". Checked: 1P race, 7,850
  audio updates in 50 s all successful, baseline log errors, clean exit. **Not heard yet** (needs the user's ears).
- Found on the way: reply values must sit at their natural alignment (a bool then a u32 puts the u32 at offset 4); packed,
  the font count read as 0x601 and the game waited forever.
- Diagnostics: `KARTRECOMP_NATIVE_SERVICES_TRACE=1` (first call of each command) / `=2` (repeats too; the engine's logger folds
  repeated lines), `KARTRECOMP_NATIVE_SERVICES_DUMP=<file>` (every request and reply), unknown commands and missing objects are
  logged.
- Known: at the INT stop the emulated services are torn down while game threads still run; a failed request there makes the
  game stop with a panic (only at shutdown, logged).

## No emulated GPU (2026-11-14)

With the native renderer and the native graphics queue (the Linux default) the emulated GPU (`Tegra::GPU`: its Null
renderer, rasterizer, command processor, GPU thread) is no longer created; `KARTRECOMP_EMULATED_GPU=1` creates it anyway.
`System::HasGPU()` guards its users: the GPU channel device (`nvhost_gpu`: no channel; a submission is not executed, but its
fence advances as the GPU's puller would advance it, so waits on it end), the address-space device (no rasterizer bound), the
display device, the CPU manager, the memory system's GPU-cache hooks, shader-cache precompile, screenshots, the window title.
The GPU and device memory maps call into a rasterizer only when one is bound.
- What the game's GPU driver still asks the driver service for (`KARTRECOMP_NVDRV_TRACE=1`, `<log dir>/nvdrv_trace.txt`): in
  a race, graphics memory blocks created, allocated and mapped into GPU address space about 25 times a second (`nvmap`,
  `nvhost_as_gpu`), syncpoint waits, and at boot the channel setup. Video (the title movie) goes through the hardware video
  decoder device (`nvhost_nvdec`, suyu's Host1x + FFmpeg), which does not need the emulated GPU.
- Checked: 1P race 4.0.1 and 3.0.4 (60 fps, pictures), 4P split-screen (59.9 fps), baseline log errors. Memory: 20-60 MB less
  resident in the effects race (noisy between snapshots).

## Every service request is answered by the runtime (2026-11-14)

- **GPU driver service** (`nvdrv`, `core/hle/kernel/native_nvdrv_service.cpp`): the service around suyu's driver module
  (graphics memory handles, GPU address spaces, syncpoints and their events, the GPU channel, the video decoder), used as a
  library.
- **Service manager** (`sm:`): `ConnectToNamedPort("sm:")` returns a runtime session; GetService hands out runtime sessions,
  and for a name the runtime does not have, a session to the emulated service (logged once per name: none in a race).
- Result: a 1P race sends no request through the emulated IPC transport (`KARTRECOMP_IPC_TRACE=1` writes no trace at all),
  60 fps, baseline log errors. What the emulated kernel still does for the game: the waits and resets on the events that the
  library parts (audio_core, hid_core, the driver module) signal (`WaitSynchronization` / `ResetSignal`, ~200/s), thread
  creation and its bookkeeping (the threads themselves are host threads), the memory system calls and the handle table for
  those kernel objects (shared memory, transfer memory, events), and loading the game's process.
- **Events** (same day): the events the library parts signal (audio rendered / device events, the controller style-set update
  events, the driver's syncpoint events) reach the game as runtime "mirror" events (`MirrorKernelEvent`): `KReadableEvent::
  Signal/Reset` update the mirror, a runtime ResetSignal resets the emulated event too. The game waits on and resets them
  natively. The only emulated object it still waits on is a thread (waiting for a thread to end), logged once per kind
  (`native services: the game waits on an emulated ...`).

## The emulated system services are no longer started (2026-11-15)

With native services on, only the 6 emulated service modules the runtime uses as libraries are started: `sm` (the registry
the runtime looks library objects up in), `hid` (hid_core's resource manager and its update thread), `nvservices` (the driver
module), `ns` (the shared-font loader), `am` (starts the game's process through its window system and registers it with the
controller library) and `settings` (the controller module waits for it). The other 52 (audio, file system, vi/nvnflinger,
account, time/glue/psc, mii, sockets, ldn, ...) are not started (`KARTRECOMP_HLE_SERVICES=<list>|all`).
Checked: 1P 4.0.1 and 3.0.4, 4P split-screen, the keyless boot; 149 -> 94 host threads, 2 fewer log errors (the socket service's).
- Deleted from the Linux engine build (CMake option `KARTRECOMP_DROP_UNUSED_SERVICES`, default on for Linux desktop only): the
  37 emulated service modules that only the start list referred to (bcat, ssl, ldn, nifm, ro, olsc, spl, pctl, btm, friend, nim,
  ptm, lm, aoc, lbl, es, fatal, pcv, usb, prepo, mm, btdrv, npns, omm, ngc, ncm, erpt, fgm, i2c, pcie, mig, bpc, ldr, eupld,
  mnpp, grc, gpio): about 20,000 lines no longer compiled. Engine 55 MB. Checked: 1P and 4P races.
