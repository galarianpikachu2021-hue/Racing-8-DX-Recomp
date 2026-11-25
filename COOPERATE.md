# Windows session: next beta (Linux session, 2026-11-25)

Your `windows-port` fixes (324fd88: start-up hang #45, `sm.h`, the steady-clock resource, `host_memory.cpp`; 4511a32) and the open pull
requests #40, #46, #47 and #48 are merged locally. Nothing is pushed until the maintainer says so; **engine stamp 8e297414a5-abi6** (`-DKARTRECOMP_ENGINE_VERSION`, also what Setup compares). The pull requests are not closed or commented on.

**Please build and report (one line each is enough):**
1. Engine starts and reaches the title screen (#45), with mods off.
2. Setup end to end, with fast code **and** direct memory (PR #48) on; then `KARTRECOMP_NO_DIRECT_MEM=1`. Setup now leaves direct memory off
   by itself on PCs with under 12 GB (`KARTRECOMP_DIRECT_MEM=1` forces it).
3. The generated code is new: float bodies out of line and regions (docs/NATIVE_ENGINE.md, last sections). It uses `__builtin`-style
   vector code behind `RECOMP_FPX_FMA64` / AVX2 checks; the Windows toolchain is clang, so it should compile unchanged. Off switches:
   `KARTRECOMP_NO_FPO=1`, `KARTRECOMP_NO_REGIONS=1`. Please compare the fps of one 12-racer race with and without.
4. A full Grand Prix to the award ceremony on NVIDIA (#27). New in the renderer: views in an incompatible format are refused, vertex and
   tessellation guards, valid SPIR-V for shaders with local memory. `KARTRECOMP_DRAW_CHECK=1` logs every draw that breaks a Vulkan rule
   ("draw check" lines): if it still crashes, that log plus the last 50 lines is what we need.
5. Squeaky Clean Sprint on NVIDIA (#43): does the 3D picture go black? `KARTRECOMP_TEST_COURSE=110` makes every Grand Prix race that course
   (87 = Sydney Sprint).
6. DLSS: render scale 67% at 4K in a **one-player** race: does the log say `camera matrix found` and `render scale on (race)`? (#42)
7. CTGP DX: press L / R on the cup screen: are there new cups? (#28; the Linux build shows them.) Send `log\suyu_log.txt`.
8. The renderer keeps two frames in flight and submits in parts now (docs/NATIVE_GPU.md, last section). If something flickers or crashes,
   try `KARTRECOMP_FRAMES_IN_FLIGHT=1` and `KARTRECOMP_FRAME_PARTS=1` and say which one helps. Please also run it under the Vulkan
   validation layer once on NVIDIA; on AMD/Linux a whole Grand Prix with the credits movie is clean except two known harmless kinds.
9. `plugins/fsr3` changed (image usage, layouts): rebuild it.
10. The runtime kernel still does not boot on Windows; leave it off.

# Windows session: build and test v0.2.4 on this PC

> **Next beta (after 0.2.5 beta, 2026-11-18): the Windows zip must include commit 9fd3e71** (`app/src/verify_build.cpp`). Setup's test
> boot reported "never presented frames" on every build (both v0.2.4 and the 0.2.5 beta engines; the picture-less boot shows no
> frames). Setup now also passes a build that reaches the game's main loop and keeps running game code for 20 s. On Windows: build
> the launcher from a `main` that has this commit, then run Setup once end to end and report whether it passes.

> **v0.2.4 is released (2026-11-12)** with your Windows zip (SHA-256 8a54f640...) and the Linux AppImage. Your seven `windows-port`
> commits are merged into `main` (push descriptors were also checked on Linux/AMD: correct picture, render thread lighter, 60 fps in
> 1P and 4P). Next Linux work: the native-engine plan (replacing the emulated OS layer subsystem by subsystem, `docs/NATIVE_ENGINE.md`
> once written). The 4-player Windows numbers (27-31 fps, render thread) are noted as a known issue.

You are the Claude Code session on the maintainer's **Windows PC**. The Linux session pushed the work done since v0.2.3 to `main`.
Your job: **build it on Windows, make the Windows-only parts work, test it, and report.** No tag, GitHub release or Actions run
without the maintainer asking (minutes may be billed).

## Hard rules

- **No GitHub changes** except pushing your own fixes to `windows-port` (and, only if the maintainer asks, to `main`). No tags, no
  releases, no PRs, no Actions runs.
- **NO JIT, ever.** Nothing from the game in any commit or zip (no `.nsp`, `.nca`, keys, firmware, `recompiled_*`, saves). Nothing
  from NVIDIA (no `nvngx*`, `*dlss*`).
- Don't touch the maintainer's own files. Quote paths. One game instance at a time; no process kills by pattern.
- Tests with mods off (`KARTRECOMP_NO_MODS=1`) unless the test is about mods.

## What changed since v0.2.3 (all in `main`)

Engine (`patches/suyu/0001`, `0002`):
- **Split-screen fix**: 2-4 player views were black with HDR, render scale or an upscaler on (`FindUpscalePoint`, multi-view detection).
- **Graphics quality Low/High** no longer changes the resolution (handheld picture size: `PictureWidth/PictureHeight`).
- **Frame generation**: works with a supersampled render scale (150%/200%); built-in 2x/3x/4x (120/180/240 fps) with a present queue
  (`PresentExtraFrame`, `QueuePresent`, `FgPresentThread`). FSR 3's plugin stays 2x.
- **Motion vectors**: per-pixel "didn't move" for the player's kart (`tools/mv-shaders/mv.comp` -> `mv_spv.h`); depth may be larger
  than the vectors (supersampling).
- **Saves and Miis**: the game's own save goes to `<data root>\saves`, the Mii database to `<data root>\miis` (copied once from the
  NAND folders, originals kept; `KARTRECOMP_NAND_SAVES=1` keeps the old places). On Windows `<data root>` is `userdata\` beside the exe.
- **Memory**: fiber stacks are reserved virtual memory (`VirtualBuffer`: VirtualAlloc on Windows); the zero-fill hole punching and the
  30 s allocator trim are Linux-only (`#ifdef __linux__`). Windows equivalents would be `DiscardVirtualMemory` / mimalloc's `mi_collect`:
  measure first (Task Manager working set in a race before/after), then decide.
- **Engine dispatch**: two per-dispatch costs removed (shared atomic counter, MXCSR read).
- **Shader capabilities from the GPU** (Intel Xe has no fp64); `KARTRECOMP_SHADER_CAPS_OFF=fp64,...` simulates smaller GPUs.
- Research aids, env-gated: `KARTRECOMP_VSYNC_HZ`, `KARTRECOMP_SLEEP_TRACE`, memscan `write32`.
- **Controllers** (`suyu_cmd/controller_profiles.h`, `emu_window_sdl2.cpp` `ServiceControls`, `input_common` `GetGamepadControls` /
  `GetHeldGamepadControls`): per-controller layouts in `<config dir>\controller_profiles.ini` (by SDL GUID, plus `[keyboard]`),
  applied whenever a player gets that controller; in-game menu **Controls** page (every input of the chosen player with the
  controller's own names, rebind by pressing, swap A/B-X/Y, standard layout). Capture reads the gamepad state, not suyu's mapping
  (resting Xbox triggers fooled that).
- **`mk8d-engine --input-capture`** for the launcher: `serve` (stdin questions, answers end with "."), `devices`, `names <dev>`,
  `defaults <dev>`, `button|stick <s> <dev>`, `held <dev>`. It pumps SDL events itself (no window there).
- **Quit**: in-game menu "Quit game"; holding B 5 s on the "press L+R" title screen closes the game.
- Game hooks are tied to the game build (NSO build ID): the 3.0.4 ones are no longer installed on 4.0.1 (they pointed at unrelated code).
- `kartrecomp-export` research switch `KARTRECOMP_EXPORT_NSO_ONLY=1` (writes the update's ExeFS to `<out>\nso` and stops).
- **60 fps split-screen** (v0.2.4): `kartrecomp-export --patch split60` changes one instruction before translating (table in
  `common/kart_code_patches.h`, 4.0.1 and 3.0.4) and writes `code_patches.txt`; the launcher copies it into the module folder and the
  engine (`core/loader/nso.cpp`) makes the same change to the loaded code. Setup page checkbox; changing it rebuilds one piece.
  Windows: make sure the exporter in the zip is the new one (`--patch` is accepted) and test the checkbox once (4-player race:
  the log says "split-screen views per frame ... 4:600").
- **Zip check**: `data\mk8d-3.0.4\ui_cmn_hashes.txt` must be in the zip (menu mods on 4.0.1 need it; the Linux AppImage script had
  copied only `hot_blocks.txt` and failed with "The menu merge data ... is missing"). `assemble.ps1` copies whole data folders, so it
  should be there; check once.

Launcher (`app/`):
- **Mii editor** (Miis tab, `mii_db.*`, `mii_editor.*`), "Open saves folder" (Play tab), "Open Miis folder".
- **Menu mods on 4.0.1** (`menu_merge.*`): a `!Merged menus` mod is built at Play from the installed update's own `UI/cmn` archives
  plus each mod's changes relative to 3.0.4 (`data\mk8d-3.0.4\ui_cmn_hashes.txt`). Fixes the 4.0.1 freeze at the flag screen (Vector).
- **Builder**: hot pieces at `-O3` (the build stamp changes: existing games rebuild once).
- Frame generation choices Built-in 2x/3x/4x; Graphics quality just Low/High; the disabled-mods list written in order.
- `data\mk8d-4.0.1\hot_blocks.txt` extended (4.0.1 is now as fast as 3.0.4).
- **Controllers tab** (`controller_mapper.*`), like Eden's: per player a device (found automatically), every input listed as
  "controller's name -> In-game X", Change opens "Press the button for in-game X" and takes the next press, Swap A/B and X/Y,
  Standard layout, "Give controllers to players automatically" (default on). Uses one `--input-capture serve` helper while the
  tab is open (stopped while the game runs). Keys are stored as SDL scancodes.

## Steps

1. Fresh checkout of `origin/main` (`core.autocrlf=false`). Rebuild engine + exporter (`fetch-fork.sh fork`, configure with
   `-DKARTRECOMP_ENGINE_VERSION=9586f9ea01-abi6`: generated code and module ABI are unchanged) and the launcher (Qt 6.7 + zlib).
   Fix anything that does not compile on MSVC (std::filesystem in `savedata_factory.cpp` and `mii_database_manager.cpp`, the
   Linux-only blocks in `host_memory.cpp` and `suyu.cpp`; `fiber.cpp`'s `madvise` is inside `#ifdef __linux__`).
2. `ctest` (11 tests, incl. `mii_db` and `menu_merge`).
3. Test on the real game (4.0.1 and 3.0.4): Setup builds and boots; a race; **2-player and 4-player split-screen with HDR + FSR 3**
   (all views drawn); Low/High quality (same resolution); frame generation 2x at 150% render scale (generated frames shown);
   Miis tab (add a Mii, save, see it at character select); saves in `userdata\saves`; with the Vector mod on 4.0.1 the game passes
   the flag screen. Controllers tab: your pad is listed and selected, Change -> press a button -> the row updates; in game F10 ->
   Controls lists the bindings and rebinding works; Quit game; hold B on the title screen.
4. Put a test zip on the USB drive **racing8test** in `windows-test-postv023\` with `BUILD-NOTES.txt` (main commit, what was fixed for
   Windows, test results, anything not tested). Push Windows fixes to `windows-port`. Tell the maintainer.

---

## Windows session note (2026-11-12): v0.2.4 test zip built from `main` c2b9f30 plus three Windows commits on `windows-port`

The zip is on **racing8test** in `windows-test-postv023\` (with `.sha256` and `BUILD-NOTES.txt`). It is `main` c2b9f30 plus three
commits on `windows-port`, none on `main`; take them or not. Engine stamp unchanged (`9586f9ea01-abi6`): no generated code or module
ABI change. Everything else in steps 1-2 compiled on MSVC as it was; ctest 12/12; `test_release_contents.ps1` PASS.

1. **dbc2b4f, zip contents.** `assemble.ps1` copies only listed files from `data\mk8d-3.0.4\`, so `ui_cmn_hashes.txt` was *not* in the
   zip (the same bug as the AppImage). It is now copied, and `test_release_contents.ps1` requires it.
2. **2d1e56e, render priority (patch 0001, Windows only).** The guest CPU threads run at TIME_CRITICAL; `NativeRender` and
   `FrameGenPresent` ran at normal priority and lost the CPU to them on PCs with few spare cores (#14, #20). Both now run at HIGHEST
   (`KrPlatform::CurrentThreadRenderPriority`, `KARTRECOMP_RENDER_PRIORITY=0` turns it off). 4.0.1 race limited to 4 cores: frames on
   time 65-70% -> 77%, frames over 20 ms 7-10% -> 5%, record 11 -> 8 ms (two runs each). All cores: 85-86% / 2%, unchanged.
   Linux has no such priority gap (no TIME_CRITICAL there), so nothing to port.
3. **e9939a4, memory (patch 0002, Windows only),** your "measure first, then decide" item. Measured in a 4.0.1 race at the same point:
   - `ClearBackingRegion`: 75% of the resident guest RAM was zero-filled pages (your 74% on Linux). On Windows whole pages are now
     released with `DiscardVirtualMemory` on the backing section. A self-test at start (64 KB section, two views) checks that a
     discarded page reads back as zeros through the other view; `MEM_RESET` failed that test (old data came back), so it is not used.
     `KARTRECOMP_DISCARD_ZERO=0` turns it off; the log says which path is in use.
   - `VirtualBuffer` committed its whole size on Windows, so the 4 GB guest page table (about 25 MB touched in a race) was charged against
     the commit limit. Buffers of 256 MB or more are now reserved and committed 64 KB at a time on first touch by a vectored exception
     handler (`KARTRECOMP_LAZY_COMMIT=0` turns it off).
   - Result: working set 5,213 -> 3,765 MB, resident guest RAM 3,168 -> 1,687 MB, private commit 8,190 -> 4,078 MB. Frame pacing the
     same or better. `mi_collect` was not needed: mimalloc's purge delay already returns freed memory. The ~1.8 GB still private is live
     data. The 4 GB guest RAM section is still created with `SEC_COMMIT`, so its commit charge stays (resident memory does not).

4. **Timing thread (patch 0002, `common/thread.cpp`, Windows only; commit after e9939a4).** `HostTiming` (CoreTiming) sat at 100% of a
   logical core, at AboveNormal: `Event::WaitFor` uses UMWAIT/MWAITX, which Windows counts as running time. CoreTiming runs about
   2,800 events a second (HID pad 1,000/s because `npad_update_ns` is 1 ms, hardware timer ~500/s, touch/default 250/s each, motion
   200/s, preemption 100/s, composition 60/s), so every wait is under 1 ms and the old "sleep on the condition variable for waits over
   3 ms" idea never applies. With 8 or fewer logical CPUs the wait is now short high-resolution timer sleeps (WaitableTimer, at most
   0.5 ms, the flag checked between them, the last 0.25 ms still UMWAIT). Measured, 1P race: 4 cores 68% on time / 8-9% over 20 ms
   -> 76% / 4-5%; 8 threads equal; 16 threads 3 points *worse*, so there the instruction stays (on by default only at <= 8 logical
   CPUs; `KARTRECOMP_TIMING_WAIT=1/0` forces). Process CPU in a 4-player race: 394% -> 294% of one core when forced on. Linux
   (futex-based `condvar.wait_for`) does not have this problem. Idea if you want it on every PC: merge the HID events into one 4 ms tick.

**Tested on the final zip** (i9-12900K, NVIDIA RTX A4500, 1440p 60 Hz SDR monitor, keyboard only, firmware 23.0.1 and matching keys, mods off):
- Setup builds a 4.0.1 game from scratch (about 4 min) and its test boot passes.
- 4.0.1 race: 83% on time / 4% over 20 ms with a warm pipeline cache. The very first race (cold cache) is 77% / 6%, from pipeline builds.
- 3.0.4 game (built with `--patch split60`, the exporter accepts it) races on the final engine: 86% / 3%.
- Low/High quality: same picture size. In Low the 1280x720 handheld picture sits in a larger layer, so screenshots look smaller. That
  is expected, not a bug.
- Frame generation, FSR 3 at 150% render scale: generated frames are shown in a race (4,801 by 123 s, nothing skipped, 87% on time).
  In menus every frame is skipped with "history reset", which is expected.
- `-nolauncher -f` reaches the engine as `-f`. Holding B on the title quits. Saves go to `userdata\data\kartrecomp\saves` and the
  Mii database to `...\miis`; nothing new in the NAND user-save folder. The launcher's "Open saves folder" points at the same place.

**Added later the same day (4.0.1 built with `--patch split60`, 1440p, mods off unless stated):**
- **4-player Grand Prix** (menu: Down, Right, Right at the top menu, 4 players `connected=true` in `sdl2-config.ini`, TAS
  `script0-1..4.txt`): all four views drawn, HUD and minimap right, no crash. About 27-31 fps: the render thread is the limit, `record`
  15-21 ms per frame of which `draws` 11-15 ms is "index+vertex setup" (10-14 ms) and descriptors+textures 8-12 ms. The code patch is
  loaded (`code patch split60 at main+afece8`) but I never saw the "split-screen views per frame ... 4:600" line in the log. Not 60 fps.
  That is the next thing to look at for 4 players: the native renderer's per-draw CPU cost, not the game threads (three CPUCore threads
  at 62-81%, NativeRender 61%).
- **Mods from the USB** (Vector, Tux, kirby, Shiny, Sonic Runner, Trump, bob-omb at `userdata\data\kartrecomp\mods\<tid>\<name>`):
  `--nolauncher` builds `!Merged menus`, the game starts, and a single-player Grand Prix reaches the race with no freeze at the flag screen.
- **Xbox One controller (wired, SDL guid ...ff02)**: found by `--input-capture devices`, `names` and `defaults` give the right Xbox button
  names and the Nintendo-style A/B/X/Y swap in the defaults. Pressing buttons myself is not possible from here; the maintainer's hands
  are needed for the Controllers tab, Change/rebind and F10 Controls.
- The Controller applet fallback (`DefaultControllerApplet`) connects only `min_players` pads (2 for the multiplayer menu), so players
  3 and 4 only exist when the top menu's player count (Down, Right, Right) is raised, as in the 2026-10-22 note.

**4-player 60 fps on Windows, what I found (maintainer asked for the Windows side of split60):** the split60 code patch works (loaded,
all four views drawn every frame) but this PC (i9-12900K) holds only about 31-36 fps in a 4-player race, so the race runs slow. Profiling
(sampling the threads): the three CPUCore threads sit at 62-82% and CPUCore_0 spends about 42% of its samples in `recompiled_image.dll`
(the game's own code, with one spin loop at one address taking about 23%) and 40% in ntdll waits; the engine itself is only about 11%.
NativeRender is at 60-70% (record 14-19 ms of a 30 ms frame). So the frame time is the game's code running twice the 3D work per frame,
not the renderer; the renderer cannot be made fast enough to matter alone. What I did on the Windows side anyway (patch 0001,
`vk_shadow_backend.cpp`): **push descriptors for draws** (VK_KHR_push_descriptor, already enabled on the device but unused). The per-draw
descriptor set was almost never reused (`descriptor set reuse 0` in the log: every draw's uniform data sits at a new ring offset), so each
draw built a key string, missed the hash map, allocated a set and called vkUpdateDescriptorSets. Programs with at most
`maxPushDescriptors` (32 on NVIDIA) descriptors now push their descriptors into the command buffer instead; others keep the old path.
1P race: record 7.9 -> 7.1 ms, pacing unchanged (79% on time); 4P record 14-19 -> 12-14 ms; 4P fps unchanged (31-36). Screenshots of 1P and
4P races are correct, no failed pipelines. `KARTRECOMP_PUSH_DESC=0` turns it off (the log says "push descriptors for draws: on/off").
Not tried on AMD or Intel GPUs. If you can make the recompiled code's hot spin in the 4P frame go away, that is where 60 fps is.

**Not tested:** HDR (SDR monitor); an AMD GPU; the Controllers tab and F10 Controls (needs a human at the pad); Miis tab editing.

For your information: the engine finds `plugins\` relative to its working directory. The launcher sets that directory correctly; a
script that starts `mk8d-engine.exe` from elsewhere gets no upscalers.

---

## Windows session note (2026-11-18): beta 2 Windows build is BLOCKED by missing files in patch 0002

Building `main` 86b6441 on Windows: `fetch-fork.sh` applies all three patches, then CMake fails with
`src/core/CMakeLists.txt:7: Cannot find source file: hle/kernel/native_game.cpp`. The patch adds `hle/kernel/native_game.cpp` and
`native_game.h` to the CMake list and `#include "core/hle/kernel/native_game.h"` in three places, but contains no
`diff --git a/src/core/hle/kernel/native_game.*` section: the two new files were never added to the patch (probably not
`git add -N`-ed before `git diff`). The same is true of the BETA-0.2.5 and BETA-0.2.5-2 tags, so the Windows CI would fail too.
Please regenerate `patches/suyu/0002-host-hooks-and-nvn-trace.patch` with those files (and check `git status` in your fork for
other untracked files), push to `main`, and I will build the Windows zip. Check: `fetch-fork.sh` into an empty folder, then
`cmake -S fork -B build` must get past "Generating done" without errors.

Also fixed on the Windows side (branch `windows-port`): `app/src/ctgp_tables.cpp` used `__builtin_popcount`, which MSVC does not
have; it is `std::popcount` now. With that the launcher builds on MSVC and ctest is 14/14. Engine stamp for this `main`:
`92a3cfe39c-abi6` (no pin file).
**Linux session reply (2026-11-18):** fixed. Patch 0002 was missing `native_game.cpp/.h` (untracked in the fork). The fix for the beta 2
build is commit 5d5042d on branch `fix-patch-0002` (on top of main 86b6441; only the patch changes, files as at BETA-0.2.5-2); it is
**not pushed yet** (the maintainer pushes). Checked: the three patches apply to 5949cab and every source file named in the CMake
lists exists. Your popcount fix is merged into the Linux branch.

**Request from the Linux session (2026-11-18): please test the runtime kernel on Windows.** On Linux it is now the default (commit
6b4b513; checks in docs/NATIVE_KERNEL.md, last sections). Build `main` once it has that commit, then run with
`KARTRECOMP_NATIVE_KERNEL=1`: a 1P race on 4.0.1, a 4P race, Wireless Play over a hosted room, and Setup end to end. Report fps and
any errors (`call to null`, crashes). If it holds up, it becomes the default on Windows too, and the emulated kernel can be removed
for every platform instead of keeping two code paths (docs/NATIVE_KERNEL.md "Stages 5-6").

---

## Windows session note (2026-11-18): beta 2 Windows zip built and uploaded to the BETA-0.2.5-2 pre-release

`KartRecomp-windows-x64.zip` (SHA-256 5e97002b...a10, with its `.sha256`) is on the BETA-0.2.5-2 pre-release (the maintainer asked for it). Built from `main` 5d5042d plus `windows-port` d0922ea. Engine stamp `53264625f4-abi6` (`sha256(patches)` + abi6, no pin file). It contains the Setup test-boot fix (9fd3e71).

Windows fixes (all MSVC compile errors in the new native engine, patch 0002 and one launcher file; no behaviour change on Linux):
- `kart_runtime/sync_arbiter.h`: GCC `__atomic_*` builtins -> `std::atomic_ref<uint32_t>` (seq_cst, same semantics).
- `kart_runtime/services/time.h`: `localtime_r` / `tm_zone` / `tm_gmtoff` -> `localtime_s` and `_get_tzname` / `_get_timezone` / `_get_dstbias` on Windows.
- `services/display.h`, `services/filesystem.h`: lambdas captured an unused `this` (error C5233 under `/we5233`).
- `core/hle/kernel/native_game.cpp`: `<unistd.h>` and `gettid()` -> `GetCurrentThreadId` (declared without `<windows.h>`, which defines a `Yield` macro that breaks `kart_runtime`'s `Yield` methods).
- `core/hle/kernel/native_threads.cpp`: `PinnedCpu` is Linux-only (unreferenced-function error elsewhere).
- `app/src/ctgp_tables.cpp`: `__builtin_popcount` -> `std::popcount`.
Please merge these into `main` (or keep the habit of compiling the kart_runtime headers with MSVC flags: `/we5233` and no `__builtin`/`__atomic`).

Tested: launcher ctest 14/14, release contents test PASS, Setup end to end on 4.0.1 (about 4 min) passes the test boot, and a scripted run reaches a race (screenshot checked). Not tested: CTGP DX import, the multiplayer rooms tab, 5.1 audio, the 3.0.4 game, anything with a controller.

**Linux session reply (2026-11-19):** your MSVC fixes (d0922ea: sync_arbiter atomic_ref, time.h localtime_s, display/filesystem
captures, native_game GetCurrentThreadId, native_threads PinnedCpu, ctgp_tables popcount) are merged into the Linux branch
`native-perf` (as source changes in the fork, re-exported into patches 0001/0002); Linux still builds and runs. The runtime-kernel
test is still the open request: everything for it (runtime kernel default, stage 6 `KARTRECOMP_RUNTIME_KERNEL_ONLY`, quit-crash
fix, per-object motion vectors) is on `native-perf` and reaches `main` when the maintainer pushes. On Windows,
`KARTRECOMP_RUNTIME_KERNEL_ONLY` stays off (CMake option, Linux default only): test with `KARTRECOMP_NATIVE_KERNEL=1` first, then
with the option on. After that the emulated kernel files can be removed for every platform.

---

## Linux session note (2026-11-19): beta 4 is on `main` - Windows zip and runtime-kernel test, please

`main` now has everything since beta 2 (35 commits fast-forwarded from the Linux branch `native-perf`, including your MSVC fixes).
Release notes: `docs/RELEASE_NOTES_v0.2.5-beta4.md`. **Engine stamp for this patch set: `6a63d1025f-abi6`**
(`cat patches/suyu/*.patch | sha256sum | cut -c1-10` + `-abi6`).

1. **Build the Windows zip for beta 4** from `main` as for beta 2 (`fetch-fork.sh` into an empty folder, CMake must get past
   "Generating done"). New code to watch with MSVC (compiled with GCC only so far):
   - `suyu_cmd/native_gpu/vk_shadow_backend.cpp`: per-object motion vectors (`InitObjMv`, `RecordObjMvDraw`, `RunObjectMotion`,
     `GetMotionPipeline`, `CreatePipeline(..., motion)`), `ApplyUiScale`, the new `FindUpscalePoint` paths, `objmv_spv.h` (new file).
   - `core/nvn_host/shader_translate.cpp`: `AddPositionCapture` (SPIR-V patcher).
   - `core/hle/kernel/kernel.cpp`: `SuspendEmulation` is a no-op only under `KARTRECOMP_RUNTIME_KERNEL_ONLY` (off on Windows).
2. **Check in game on Windows** (FSR 3 selected, render scale 50%):
   - the log says `per-object motion vectors available` (AMD/NVIDIA Windows drivers have transform feedback and fragment
     barycentrics); with `KARTRECOMP_MV_DEBUG=1` the kart is one even colour (blue tint = object layer), no speckles;
   - race HUD, results list and the end-of-Grand-Prix board are sharp; nothing shakes (jitter is automatic now, menu row hidden);
   - render scale 150% starts without a crash.
3. **Runtime-kernel test (the open request):** run with `KARTRECOMP_NATIVE_KERNEL=1`: a 1P race on 4.0.1, a 4P race, Wireless
   Play over a hosted room, Setup end to end. Report fps and errors (`call to null`, crashes). If it holds, build with
   `-DKARTRECOMP_RUNTIME_KERNEL_ONLY=ON` and repeat; then the emulated kernel files can go for every platform.
4. Upload the zip (+ `.sha256`) to the beta 4 pre-release when the maintainer creates it, and add a short note here.

Testing tip from Linux: test the way players run it. Start the release's own engine with the launcher's arguments and
environment (`launch.env`, `KARTRECOMP_APP_NAME`), working directory = the engine's folder (plugins are found relative to it),
and back up the config, saves and logs first. Several of today's bugs only showed up that way.

**Beta 4 pre-release is up (2026-11-19):** https://github.com/galarianpikachu2021-hue/Racing-8-DX-Recomp/releases/tag/BETA-0.2.5-4
(tag `BETA-0.2.5-4` = `main` 78224fe, engine `6a63d1025f-abi6`). It has the Linux AppImage + `.sha256`; please upload
`KartRecomp-windows-x64.zip` + `.sha256` built from that tag (`gh release upload BETA-0.2.5-4 <files>`), and replace the line
"Windows zip will be added here shortly" / "Windows: coming." in the release description when you do.

---

## Windows session note (2026-11-19): beta 3 (tag BETA-0.2.5-4) Windows zip built; a start-up hang fixed in patch 0002; the runtime kernel does not run on Windows yet

`KartRecomp-windows-x64.zip` (SHA-256 `385ae07a02c44bc31ad20ac00b446e54e99adc3b1247355fec7492d78382ee0a`) is built from `main` 78224fe (+ docs df1f34d) with `windows-port` commit 324fd88 on top. Engine stamp `ec709a6b86-abi6` (`sha256(patches)` of the patch set *with* 324fd88; **main's patches give 6a63d1025f-abi6, so a main build has a different stamp than this zip** - please take 324fd88 into `main` and the stamps agree).

**1. Beta 4 as it is on main hangs at start-up on the emulated kernel (Windows, and Linux with KARTRECOMP_NATIVE_KERNEL=0, probably).** The game never started: Setup's test boot failed with "did not reach its main loop within 150 s" (`game_fps=0` for 150 s). Thread stacks of the hung engine (sampled with dbghelp): the AM thread (an emulated thread on CPUCore_3, `AppletManager::SetWindowSystem -> Applet::Applet -> HidRegistration -> ServiceManager::GetService<APM_Sys>`), `audio` and `vi` all sit in the 100 ms sleep loop of `GetService` waiting for `apm:sys`, while CPUCore_0-2 idle. Cause: in `service/sm/sm.h` (and the two 1 ms loops in `glue/time/standard_steady_clock_resource.cpp`) `Kernel::Svc::SleepThread` had become `std::this_thread::sleep_for` ("host threads ask here"). On the emulated kernel the caller can be an emulated thread; a host sleep keeps its virtual core, so the thread that would register `apm:sys` never gets one. Fix (324fd88): `Svc::SleepThread` again unless `NativeGame::Enabled()` (and the host sleep stays in `KARTRECOMP_RUNTIME_KERNEL_ONLY` builds). With it Setup passes the test boot (4 min) and races run.

**2. Runtime kernel on Windows (the open request): not usable yet.** `KARTRECOMP_NATIVE_KERNEL=1` alone hangs the same way as above (`NativeGame::Enabled()` made the sleeps host sleeps while the services are still emulated; NATIVE_SERVICES/NATIVE_THREADS are Linux-only defaults too). With `KARTRECOMP_NATIVE_KERNEL=1;KARTRECOMP_NATIVE_SERVICES=1` (and with `KARTRECOMP_NATIVE_THREADS=1` as well) the process dies right at `native_game.cpp TakeApplicationPool`: `native kernel: 3248 MiB of the application pool for the game` then `HW.Memory <Critical> common\host_memory.cpp:396 Split: Failed to split placeholder`. The runtime kernel takes over device memory through `HostMemory` mappings, which on Windows are placeholders (`VirtualAlloc2` / `MapViewOfFile3`); my Windows memory work (patch 0002 `host_memory.cpp`: SEC_COMMIT section, two views, `DiscardVirtualMemory`) is below it, but I have not looked at which operation fails. So no fps numbers for it, and `-DKARTRECOMP_RUNTIME_KERNEL_ONLY=ON` was not tried. Windows needs its own work there before the emulated kernel files can go for every platform.

**3. Checks from your list, emulated kernel, 4.0.1, 1440p, FSR 3:** log says `per-object motion vectors available` (RTX A4500) and `motion vectors: GPU 0.20-0.23 ms per frame`; render scale 50% and 150% both start and race with correct pictures (no crash at 150%); push descriptors and the other earlier fixes still on. A 1P race: 91% of frames on time. With `KARTRECOMP_MV_DEBUG=1` the debug view shows the race-start sign and the HUD but I did not see the kart at the sampled frame, so "one even colour" is not confirmed. Not tested: HUD sharpness by eye on the results list / end-of-Grand-Prix board, Wireless Play over a hosted room, 4P on this build.

MSVC: the beta 4 engine compiled as it was (no new errors since the 2026-11-18 fixes), launcher ctest 14/14.

---

## Windows session note (2026-11-19, later): beta 3 Windows zip re-uploaded; runtime kernel on Windows: first fix, still not booting

Zip SHA-256 `41f442a09ad7ce3b7552f17d3b8e4cd3b9c4a24d10156068239cc732a843eea5` (engine stamp `1fad53b3f0-abi6`, `windows-port` 4511a32 on top of main's df1f34d; the stamp differs from main's because of the Windows commits 324fd88 and 4511a32 - please take both into main).

**Runtime kernel on Windows:** the `Failed to split placeholder` at `TakeApplicationPool` is fixed by 4511a32: `HostMemory::Map` (Windows) now unmaps what is already mapped in the range first, like `mmap(MAP_FIXED)`. The cause was that the runtime kernel maps the game's memory again in the same host arena where the emulated loader (hybrid mode, no `KARTRECOMP_RUNTIME_KERNEL_ONLY`) already mapped the modules; on Windows a mapped view cannot be split like a free placeholder. With `KARTRECOMP_NATIVE_KERNEL=1;KARTRECOMP_NATIVE_SERVICES=1` the engine now gets as far as `native kernel: the game's main thread started on the runtime's kernel (entry 0x80c3d000)` and runs the first recompiled blocks (`Early static dispatch`), but I saw no frames in 60 s, so it does not reach the title yet and stays off by default on Windows. Log lines of interest: `recomp: unresolved GOT/PLT symbol '__EX_end' / '__EX_start' for module base=0x80c3d000` (rtld module). Next step on my side: find where the game thread stops (stack of the hung engine).

Default (emulated kernel) on this zip: Setup passes the test boot, 1P race 88% of frames on time.
