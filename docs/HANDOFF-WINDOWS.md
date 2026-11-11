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
- **DLSS/NVIDIA libraries are never redistributed**: nothing from NVIDIA in the repo or the release zip. The DLSS plugin is built for local testing only.
- Pushing to `windows-port` is allowed. **Never push `main`, create releases/tags, remotes or PRs** unless told. No v0.2.0 release until told.
- Do not touch the `awesome-decomp-games` repo. `nsp-builder` branch stays local.
- Windows test builds for the maintainer go on the USB drive `racing8test`, never with game files in them.

## How the Windows build is made
Windows binaries are **built locally on the maintainer's Windows PC** with Visual Studio 2022 Build Tools, CMake, Ninja, the Vulkan SDK and
Qt 6.7 (zlib built from source, static, /MD). GitHub Actions (`.github/workflows/windows-engine.yml`) still has the exact recipe (jobs `engine`,
`toolchain`, `launcher`, `ffx-prepared`, `plugins`, `bundle`) but **runs only by hand**, because Actions minutes may be billed.
The engine source of truth is `patches/suyu/0001..0003` applied to the pinned upstream by `packaging/windows/fetch-fork.sh`; to change engine
code either edit the fork after running that script, or edit the patch hunks (no empty context lines: Windows checkouts are CRLF).
The engine stamp is passed explicitly: Windows releases keep `9586f9ea01-abi6` as long as generated code and the module ABI do not change, so
players' built games stay valid. `COOPERATE.md` holds the current build-and-hand-over instructions (the zip goes on the USB drive
`racing8test`, the Linux session checks it and publishes it). DLSS is built by the user with `plugins/dlss/build-windows.ps1` and never ships.

## Runtime layout (portable)
`kartrecomp.exe` and `engine\mk8d-engine.exe` sit in one folder; everything written goes to `userdata\{data,cache,config}\kartrecomp` beside them
(`launch.env` holds the launcher's settings; `KARTRECOMP_APP_NAME=kartrecomp` selects the folder). Logs: `userdata\data\kartrecomp\log\suyu_log.txt` (engine),
the launcher's own log next to it. `Collect-Logs.cmd` zips logs + settings + `system-info.txt` (CPU/GPU/driver/refresh rate/power plan) without game files.
In-game overlay (settings): F10 or L3+R3. Plugins: `engine\plugins\kr_{upscaler,framegen}_<id>.dll`.

## OPEN PROBLEMS (as of v0.2.3)
Fixed since the first handoff: frame generation above 1080p (`7791dd1`, the generated frame's window crop), the slowdowns (pipeline key,
static vertex buffers, mimalloc: on-time frames about 25% -> 65% on the test PC), the settings-save crash, Play looking for `mk8d.exe` (#6, #10).
1. **#8: divide-by-zero crash at start** (`0xc0000094` at `mk8d-engine.exe+0x2e00eb`, called from `+0x2dff06`) on some PCs, at the test boot
   or at Play, even with firmware 20.0.1-22.5. Symbolize with the build's `.pdb` first.
2. **#12: black areas over the picture** on an RX 7800 XT (Windows 11, v0.2.2), worse in fullscreen.
3. **#14, #20: poor frame pacing** on some Intel + NVIDIA PCs (shader stutter, long loads).
4. A crash when closing the game (shutdown-order race in `Core::HID::EmulatedController::GetHomeButtons` from `Service::AM::ButtonPoller`).
5. Not run on real GPUs yet: FSR 4 frame generation (needs an RX 9000).

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
