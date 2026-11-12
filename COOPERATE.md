# Windows session: build and test v0.2.4 on this PC

You are the Claude Code session on the maintainer's **Windows PC**. The Linux session pushed the work done since v0.2.3 to `main`.
Your job: **build it on Windows, make the Windows-only parts work, test it, and report.** This is not a release yet: no tag, no
GitHub release, no Actions run (minutes may be billed).

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

**Not tested:** 2P/4P split-screen (the "4:600" split60 log line and the HDR + FSR 3 views); HDR (SDR monitor); an AMD GPU; the
Controllers tab and F10 Controls (no gamepad on this PC); Miis tab editing; Vector mod on 4.0.1 (not on this PC).

For your information: the engine finds `plugins\` relative to its working directory. The launcher sets that directory correctly; a
script that starts `mk8d-engine.exe` from elsewhere gets no upscalers.
