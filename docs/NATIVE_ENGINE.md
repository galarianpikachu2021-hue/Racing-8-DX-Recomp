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
