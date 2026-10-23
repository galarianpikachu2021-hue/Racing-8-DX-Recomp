# Roadmap

## v0.1: launcher skeleton (now)
- Qt6 app with Play / Display / Frame rate / Controllers / Miis / DLC / Setup tabs.
- Reads and writes the game's `user/config/sdl2-config.ini`. Starts the static binary with the explicit
  content arguments and any DLC.

## v0.2: settings that matter
- **Resolution:** `resolution_setup` (enum `Res1_4X`…`Res8X`, where `Res1X` = 3), plus aspect ratio,
  scaling filter, AA, anisotropy and vsync.
- **Controllers:** list SDL devices and capture button and axis bindings into `player_N_*` keys. Support
  multiple players and docked/handheld.
- **Frame rate:** speed limit / unlocked (`use_speed_limit`, `speed_limit`). True custom frame rates above
  60 need game-side changes, because MK8D's simulation is tied to 60 Hz. Options: (a) a runtime patch layer
  applied at load (suyu supports IPS/cheats under `load/`), or (b) patches applied to the generated C before
  compiling. Every patch must be generated from the user's own game, never shipped as game bytes.
- **Miis:** manual Mii entry. Mii data lives in the system save `8000000000000030` (`MiiDatabase.dat`) in
  the package's NAND. Build an editor for the CharInfo fields (name, face, hair, colors) that writes valid
  records with correct CRCs.

## v0.3: one-click build
- Setup wizard: pick the dump, keys and firmware, and verify versions (3.0.4 check) and key presence.
- Drive the pipeline in docs/PIPELINE.md from the app: build suyu, export, registry, compile, package.
  Show progress, cap memory, make it resumable.

## v0.4: Windows
- MSVC/clang-cl build of the launcher and of suyu-cmd-static. The upstream Windows "Build" export already
  writes the registry.
- Replace `systemd-run` memory caps with Job Objects. Use the `%APPDATA%` and portable-data paths.
  Package with `windeployqt`.

## Native GPU (in progress, see NATIVE_GPU.md)
- Done: NVN → Vulkan for full race frames with the emulated GPU off (`KARTRECOMP_NATIVE_GPU=native`, or
  Renderer: Native Vulkan in the launcher), about 60 fps.
- Next:
  - ASTC textures (UI icons).
  - Compute dispatches.
  - Parallel pipeline builds for course-start hitches.
  - Persistent vertex/index buffers (cuts about 20–30 MB/frame of uploads).
  - Resolution scaling and FSR for the native path.
  - A menu and HUD pass with real input.
- Then: the backend interface for Metal and Android (GLES/Vulkan on ARM). The model and shader
  translation are already backend-neutral.

## Open technical issues
- NAND-installed update boot hang in suyu-cmd (see PIPELINE.md).
- Strict static was tested in attract mode plus one human-driven race. Still to test: full cups, online
  (expected unavailable), every DLC course, and save round-trips.
