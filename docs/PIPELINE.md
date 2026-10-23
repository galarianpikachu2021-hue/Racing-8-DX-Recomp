# Build pipeline (verified on Linux, 2026-10-18)

Inputs, all supplied by the user and never committed: the MK8D base NSP (v0), the 3.0.4 update NSP
(version 1310720), optional DLC NSPs, `prod.keys`/`title.keys`, and firmware.

> Only update **3.0.4** works. The v1.0.0 base and update 3.0.3 are 32-bit ARM (NPDM flags `0x04`), and the
> recompiler only translates AArch64. Check an update's real version with `tools/nsp-cnmt-version.py`.

## Upstream pieces

- suyu fork: `https://github.com/dougchansan/suyu-v0.0.4.git`, branch `mk8-recomp`, pinned at `5949cab3ba`
  (v0.0.11). License: GPL. Apply `patches/suyu/*.patch` on top.
- mk8-recomp: `https://github.com/dougchansan/mk8-recomp.git`. It has no license file, so reference it as
  a submodule and do not copy its scripts into this repo. It holds the reference scripts (`export-recomp.py`,
  `mcp.py`, `install-nsp.py`, `build-suyu.sh`, `build-recomp.sh`).

## Steps

1. **Build suyu**, the GUI and the no-JIT variant. About 10 minutes each at `-j3`, with a peak around 4 GB.
   Arch-family distros also need the Qt private header path; mk8-recomp's `build-suyu.sh` handles that with
   `GUI=1`.
2. **Profiles.** Two sandboxed suyu data dirs (`XDG_DATA_HOME`/`XDG_CONFIG_HOME`/`XDG_CACHE_HOME`):
   - an export profile with the 3.0.4 update installed to NAND (mk8-recomp `install-nsp.py`);
   - a clean "home" profile with keys and firmware and no update. The game launches against this one.
   Never copy a GUI config between profiles: it stores absolute paths.
3. **Export.** Start the suyu GUI (`-hacker` skips modal dialogs) on the export profile with
   `SUYU_AOT_TRANSLATE_ALL=1`, then drive its MCP server on 127.0.0.1:9742:
   `export-recomp.py --rom <base.nsp> --out <dir> source`. The ROM must be the **base** NSP with the update
   installed. Passing an update-only NSP crashes the GUI.
   Without `SUYU_AOT_TRANSLATE_ALL=1`, vector `SHL #imm` is left to the JIT on purpose, and a no-JIT build
   cannot run.
   Only one suyu GUI may be running: a second one can't bind the MCP port, and export requests silently go
   to the old instance.
4. **Static registry.** The exporter writes `recomp_registration.c` and the `recomp_*_v1.h` markers only
   for the Windows "Build" format. On Linux, run `tools/make-static-registry.py <export>/usr/bin/aot_cache/exefs`.
5. **Compile + link.**
   `cmake -S suyu -B build/suyu-nojit -DSUYU_NO_JIT=ON -DSUYU_CMD_RECOMP_DIR=<exefs> "-DRECOMP_OPT_FLAGS=-O1 -foptimize-sibling-calls"`
   then `ninja -C build/suyu-nojit -j1 suyu-cmd-static`. This takes about 2.5 hours at `-j1`. A single
   `main` translation unit peaks around 5–7 GB with GCC, so cap jobs by RAM, not by core count. After
   changing only suyu sources, relinking takes about 2 minutes.
6. **Package.** Layout:
   `game/{mk8d, play.sh, content/{base.nsp, update-3.0.4.nsp, dlc/*.nsp}, user/}`. The exe treats `user/`
   as its portable data dir (saves, config, logs). Keys and firmware come from `SUYU_HOME`.
7. **Run.**
   `mk8d --content-base content/base.nsp --content-update content/update-3.0.4.nsp [--content-dlc x.nsp ...]`.
   Check coverage after a run: `user/log/recomp_coverage.txt` should say `fully static`.

## Known issues

- **Applets must be HLE in strict static builds.** LLE applets are separate firmware programs with no CPU
  backend when there's no JIT. Opening one (the in-game manual/info = offline web applet) hung the game with
  "Cannot run thread … without a CPU interface". Patch 0001 forces every applet to HLE in
  `SUYU_CMD_STATIC_RECOMP_STRICT` builds. The HLE web/manual applet has no browser in suyu-cmd and simply
  closes.

- With the update **installed in NAND**, `suyu-cmd` (static or JIT) never presents a frame. In the static
  build, null reads loop about 3 s in. The GUI with the same NAND works. Root cause unknown. The explicit
  `--content-*` path avoids it, and our suyu patch enables that path for static builds.
- An occasional magenta/overexposed track is a known emulator GPU issue, not a recomp issue. It also
  appears under the JIT.
- suyu GUI shutdown sometimes aborts in `~ServerManager`. That's an upstream teardown bug and harmless.

## AppImage of the finished game (local, personal use; 2026-10-23)
`packaging/make-appimage.sh <game dir> <out dir>` packs the game program built on this machine (`mk8d`), its libraries
(linuxdeploy, ~150 libs) and the plugins into `KartRecomp-x86_64.AppImage` (~350 MB, zstd). Tools are fetched to
`~/.cache/kartrecomp-tools`. Never distribute the result (it contains the translated game code, LEGAL.md).
Runtime layout (AppRun sets `KARTRECOMP_APP_NAME=kartrecomp`, which switches the fork from the portable `user/` folder to XDG):
- `~/.local/share/kartrecomp/`: `content/{base.nsp,update-3.0.4.nsp,dlc/*.nsp}`, `keys/prod.keys`, `nand/` (firmware in
  `nand/system/Contents`, saves in `nand/*/save`), `mods/<title id>/<mod>/`, `log/`.
- `~/.config/kartrecomp/`: `sdl2-config.ini`, `kartrecomp_overlay.ini`, optional `launch.env` (VAR=value lines, e.g.
  `KARTRECOMP_NATIVE_HEIGHT=1440`, `KARTRECOMP_PIN=2,3,4,5`).
- `~/.cache/kartrecomp/`: shader/pipeline caches.
Built on Arch (current glibc): runs on distros with the same or newer glibc; an older-base build is needed for others.

## Fast compile of the generated code (2026-10-23)
- The recompiler now writes 5000-block units (was 20000; `kBlocksPerUnit` in `arm64_to_c.h`) and the generated CMake assumes
  2 GB per compile job: one unit piece peaks under 1 GB (gcc) / 0.4 GB (clang). Older exports: `tools/recomp/split_export.py`.
- Block hooks are opt-in at export time (`SUYU_RECOMP_BLOCK_HOOKS=1`); without them each unit compiles ~4x faster (the hooked
  main took ~470 s and 5.3 GB per 20000-block unit, unhooked 107 s and 2.2 GB).
- Clang + tiers: `-DCMAKE_C_COMPILER=clang`, `split_export.py --hot=data/mk8d-3.0.4/hot_blocks.txt` (hot blocks `-O2`,
  the rest `RECOMP_COLD_FLAGS`, default `-O0`). One piece: gcc -O2 26 s, gcc -O0 6 s, clang -O2 12 s, clang -O0 2 s.
  Whole game code: gcc -O2 ~205 CPU-min; clang tiered 26.6 CPU-min = 6.7 min wall at -j4 (8 threads, 16 GB).
- Cost at run time (held-out cups 4/7, mods off, 2 runs each): clang tiered +2.9% (effects race) / +3.1% (plain race) core-0
  CPU vs gcc -O2; effects race 15.5 vs 6.5 s below 58 fps. ~1-2% of core-0 time is in cold (-O0) blocks, mostly the SDK
  module (no coverage there).
- Where the cost comes from: a clang build with EVERYTHING at -O2 measured the same as the tiered one (effects race 41,639 vs
  42,117 core-0 samples, plain race 38,386 vs 37,438; gcc 40,944 / 36,316). So the tiers cost ~nothing; clang's code for these
  blocks is ~3% slower than gcc's. Default for the pipeline (and the only option on Android): clang tiered. Desktop users who
  want the last 3% can build with gcc -O2 (~205 CPU-min).
