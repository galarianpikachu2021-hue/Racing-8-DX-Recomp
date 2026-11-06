# Handoff: working on the Windows build (read this first)

You are Claude Code running on the maintainer's **Windows 11 PC** (Intel CPU, NVIDIA GPU; an AMD PC is tested separately). The project was developed on Linux
until now; this file brings you up to speed. The maintainer's pronouns are unknown: use "they". Be concise, report faithfully (say when something failed or was
not tested), and ask before any decision that is not obviously yours.

## What this is
**Racing 8 DX Recomp** (launcher internally "KartRecomp"): a Qt6 launcher plus a fork of the suyu/Eden emulator that runs a **statically recompiled (no JIT)**
Mario Kart 8 Deluxe 3.0.4 built on the user's own PC from their own dump. Repo: `galarianpikachu2021-hue/Racing-8-DX-Recomp`, branch `windows-port` (this is where
Windows work happens). Read `CLAUDE.md`, `docs/NATIVE_GPU.md` (renderer), `docs/UPSCALER.md` and `docs/WINDOWS_PLUGINS.md` (upscalers) as needed.

## Hard rules (from the maintainer)
- **NO JIT, on any platform, ever.** Everything else is negotiable, but ask first.
- Never commit or publish game data, keys, firmware, or anything generated from the game (generated C, objects, `recompiled_*.dll`, saves). They live in `userdata\`.
- **DLSS/NVIDIA libraries are never redistributed**: nothing from NVIDIA in the repo, the release zip or CI release artifacts. The DLSS plugin is built for local testing only.
- Pushing to `windows-port` is allowed. **Never push `main`, create releases/tags, remotes or PRs** unless told. No v0.2.0 release until told.
- Do not touch the `awesome-decomp-games` repo. `nsp-builder` branch stays local.
- Windows test builds for the maintainer go on the USB drive `racing 8 testing`, never with game files in them.

## How the Windows build is made
Windows binaries are **built by GitHub Actions** (`.github/workflows/windows-engine.yml`), not on this PC (no toolchain is assumed): jobs `engine` (MSVC, fork =
upstream pin + `patches/suyu/*.patch`), `toolchain` (llvm-mingw that compiles the game on the user's PC), `launcher` (Qt), `ffx-prepared` (Linux: FSR 3 shader blobs),
`plugins` (FSR 3 / FSR 4), `bundle` (zip). A run takes ~45 min. `gh run list --branch windows-port`, `gh run download <id> -n KartRecomp-windows-x64-TEST`.
A commit message containing `[dlss]` also builds the DLSS plugin (1-day artifact for local use; **download, use, then delete it**).
The engine source of truth is `patches/suyu/0001..0003` applied to the pinned upstream by `packaging/windows/fetch-fork.sh`; to change engine code on this PC either
edit the fork after running that script, or edit the patch hunks. `packaging/windows/engine-version.pin` pins the engine stamp for TEST builds so a renderer-only
change does not force the game to be rebuilt; delete it before a real release or if generated code / the module ABI changes.
If you want to build locally anyway: Visual Studio 2022 Build Tools, CMake, Ninja, vcpkg, Vulkan SDK, Qt 6.7 (see the workflow for exact steps).

## Runtime layout (portable)
`kartrecomp.exe` and `engine\mk8d-engine.exe` sit in one folder; everything written goes to `userdata\{data,cache,config}\kartrecomp` beside them
(`launch.env` holds the launcher's settings; `KARTRECOMP_APP_NAME=kartrecomp` selects the folder). Logs: `userdata\data\kartrecomp\log\suyu_log.txt` (engine),
the launcher's own log next to it. `Collect-Logs.cmd` zips logs + settings + `system-info.txt` (CPU/GPU/driver/refresh rate/power plan) without game files.
In-game overlay (settings): F10 or L3+R3. Plugins: `engine\plugins\kr_{upscaler,framegen}_<id>.dll`.

## OPEN PROBLEMS (the reason you are here)
1. **Frame generation is completely broken on Windows**, with every backend (built-in, FSR 3, FSR 4, DLSS; the maintainer says "no matter what version").
   The exact symptom is not recorded: ASK the maintainer what they see (black frames? flicker? stutter? crash? lower fps?) and get a log.
   How it works: `docs/UPSCALER.md` "Frame generation". Each game frame the renderer draws the real frame, then (`FgPresentThread` in
   `vk_shadow_backend.cpp`) presents the interpolated frame half a frame period later from its own thread, holding `queue_mutex` around `vkQueuePresentKHR`,
   and presents the real frame afterwards. Things worth checking first on Windows: (a) the swapchain present mode (the log now prints
   `swapchain present mode ... (available: ...)`; FIFO would make the present call block for vsync while holding the queue lock, stalling the renderer), (b) the new log
   line every 300 frames `frame generation presents N: present call avg/max, queue lock wait max, wake-up lateness max` (timer resolution is raised in `suyu.cpp`, so
   lateness should be < 1 ms), (c) the display's refresh rate vs 2x the game fps (60 fps game on a 60 Hz panel cannot show 120 fps: set the monitor to 120/144 Hz or use mailbox),
   (d) NVIDIA-driver behaviour with two presents per frame in windowed vs exclusive fullscreen, (e) DWM flip model / "Windows optimisations for windowed games".
2. **Slowdowns like the ones seen on Linux before the fixes** (frame dips, bad in split-screen and near the finish line). On Linux they came from per-bind uniform
   snapshots (fixed, in the patch: `SnapshotOnce`) and glibc malloc contention (fixed with tcmalloc via LD_PRELOAD, **not yet replaced on Windows**: candidates are mimalloc
   linked into `mk8d-engine.exe`, or a low-fragmentation-heap check) and, on Intel hybrid CPUs, threads landing on E-cores (new: `PinToPerformanceCores` now also
   works on Windows via the EfficiencyClass; it prints `hybrid CPU: restricted to the performance cores ...` on stderr; `KARTRECOMP_PIN_PCORES=0` disables it).
   Use `KARTRECOMP_FRAME_TRACE=<frame>` (logs 240 frames of guest interval vs renderer time), the 300-frame `nvn-host vulkan: per frame ...` lines, and
   Windows tools (Task Manager per-core view, `wpr -start CPU` / WPA, or Process Explorer) to see which thread is saturated. Linux findings: `docs/NATIVE_GPU.md` and the
   CLAUDE.md "Current work" section (CPU time is mostly data-cache stalls in the recompiled code; guest threads + NVN hooks + renderer record are the three loads).
3. Never run on a real GPU yet: the FSR 4 upscaler/frame generation (needs an AMD card; on NVIDIA it falls back to FSR 3.1.6) and the DLSS plugin (NVIDIA: this PC).
   The in-game Model row (F10, under Upscaler) lists models per plugin. These are the first things to try here.

## Useful environment variables (set in `userdata\config\kartrecomp\launch.env`, `KEY=value` per line, or in a console before starting the engine)
`KARTRECOMP_NATIVE_GPU=native` (default via launcher), `KARTRECOMP_NATIVE_HEIGHT=<720|1080|1440|2160>`, `KARTRECOMP_UPSCALER=<fsr1|fsr3|fsr4|dlss|passthrough>`,
`KARTRECOMP_FRAME_TRACE=<frame>`, `KARTRECOMP_RENDER_PAD_MS=<ms>` (adds render time, an experiment), `KARTRECOMP_NATIVE_SHOTS=<n>` (+ `KARTRECOMP_OVERLAY_SHOTS=1`),
`KARTRECOMP_OVERLAY_SCRIPT=toggle,down,right,...`, `KARTRECOMP_FSR3_DEBUG/KARTRECOMP_FSR4_DEBUG=1`, `KARTRECOMP_PIN_PCORES=0`. Overlay settings persist in
`userdata\config\kartrecomp\kartrecomp_overlay.ini` (keys: upscaler, model, quality, framegen, present, ...). The engine rewrites `sdl2-config.ini` on exit: back it up before tests
that touch controllers.

## What to bring back to the Linux machine (where the generated-code pipeline and most profiling tools live)
Commit your changes on a branch of `windows-port` (small commits, clear messages; engine changes as edits to `patches/suyu/0001-...patch` or a new numbered patch), and
copy back: the repo (`git bundle create windows-work.bundle --all` onto the USB drive), the log zips (`Collect-Logs.cmd`), and a short NOTES file saying what you
tried, what the measurements were, and what is still wrong. Do not put game files, keys or firmware on the drive next to the bundle's notes.

## Process safety
Keep one game instance at a time; back up `userdata\config` before experiments; quote paths with spaces; do not kill processes by pattern; long jobs get checked
after a few minutes, never blocked on indefinitely; no more than the needed GPU load while the maintainer's other programs run.
