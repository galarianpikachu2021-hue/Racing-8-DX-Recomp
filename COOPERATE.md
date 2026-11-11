# Windows session: build and test the post-0.2.3 changes on this PC

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

Launcher (`app/`):
- **Mii editor** (Miis tab, `mii_db.*`, `mii_editor.*`), "Open saves folder" (Play tab), "Open Miis folder".
- **Menu mods on 4.0.1** (`menu_merge.*`): a `!Merged menus` mod is built at Play from the installed update's own `UI/cmn` archives
  plus each mod's changes relative to 3.0.4 (`data\mk8d-3.0.4\ui_cmn_hashes.txt`). Fixes the 4.0.1 freeze at the flag screen (Vector).
- **Builder**: hot pieces at `-O3` (the build stamp changes: existing games rebuild once).
- Frame generation choices Built-in 2x/3x/4x; Graphics quality just Low/High; the disabled-mods list written in order.
- `data\mk8d-4.0.1\hot_blocks.txt` extended (4.0.1 is now as fast as 3.0.4).

## Steps

1. Fresh checkout of `origin/main` (`core.autocrlf=false`). Rebuild engine + exporter (`fetch-fork.sh fork`, configure with
   `-DKARTRECOMP_ENGINE_VERSION=9586f9ea01-abi6`: generated code and module ABI are unchanged) and the launcher (Qt 6.7 + zlib).
   Fix anything that does not compile on MSVC (std::filesystem in `savedata_factory.cpp` and `mii_database_manager.cpp`, the
   Linux-only blocks in `host_memory.cpp` and `suyu.cpp`; `fiber.cpp`'s `madvise` is inside `#ifdef __linux__`).
2. `ctest` (11 tests, incl. `mii_db` and `menu_merge`).
3. Test on the real game (4.0.1 and 3.0.4): Setup builds and boots; a race; **2-player and 4-player split-screen with HDR + FSR 3**
   (all views drawn); Low/High quality (same resolution); frame generation 2x at 150% render scale (generated frames shown);
   Miis tab (add a Mii, save, see it at character select); saves in `userdata\saves`; with the Vector mod on 4.0.1 the game passes
   the flag screen.
4. Put a test zip on the USB drive **racing8test** in `windows-test-postv023\` with `BUILD-NOTES.txt` (main commit, what was fixed for
   Windows, test results, anything not tested). Push Windows fixes to `windows-port`. Tell the maintainer.
