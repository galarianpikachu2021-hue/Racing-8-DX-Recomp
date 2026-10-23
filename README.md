# Racing 8 DX Recomp

(KartRecomp is the launcher's name inside the project.) Not affiliated with or endorsed by Nintendo.

A launcher and build tool that turns **your own** dump of Mario Kart 8 Deluxe (v3.0.4) into a native,
statically recompiled PC executable. The result runs with **no JIT**: every game instruction executes as
compiled C. The project ships no game files, keys, firmware or recompiled code. See [LEGAL.md](LEGAL.md).

**Status:** early. The pipeline works end to end on Linux. A full static build boots and plays at 60 fps,
and DLC loads. The launcher builds, configures and packages the game (AppImage), manages mods and updates itself from
this repository's releases. Once built, the game can be packed into an AppImage for your own machine (never share it: it
contains the translated game code). Windows support is planned; see [docs/ROADMAP.md](docs/ROADMAP.md).

## How it works

1. You supply the base game, the 3.0.4 update, optional DLC, and your own keys and firmware.
2. The suyu fork's AOT exporter translates the game's AArch64 executables (`rtld`, `main`, `subsdk0`,
   `sdk`) into C.
3. The C is compiled and linked with suyu's HLE services into one executable, `suyu-cmd-static`, in
   strict-static mode (`-DSUYU_NO_JIT=ON`).
4. The launcher starts that executable with your settings, and passes your base game, update and DLC files
   directly. Nothing gets installed into an emulator profile.

The full recipe, including memory and time costs, is in [docs/PIPELINE.md](docs/PIPELINE.md).

## Features

| Feature | State |
|---|---|
| Strict static build (no JIT), 60 fps | working (Linux) |
| DLC via `content/dlc/*.nsp` | working: the game sees the add-on content |
| Saves, config and logs in a portable `user/` folder | working |
| Controller auto-mapping (SDL) | working |
| Launcher: resolution, aspect ratio, filters, vsync | in progress |
| Launcher: controller remapping UI | planned |
| Launcher: manual Mii entry | planned |
| Custom frame rates (above 60) | planned. Needs game-side patches, see the roadmap. |
| Windows | planned. Qt6 and the suyu fork both build on Windows. |
| Native GPU (NVN → Vulkan/Metal, no emulated GPU) | experimental, Linux/Vulkan: full races render natively at 60 fps with the emulated GPU off. Pick **Renderer: Native Vulkan** on the Play tab, or set `KARTRECOMP_NATIVE_GPU=native`. Menus (text, icons, movie previews), the race HUD and courses match the emulated renderer closely; a course's first frame can pause briefly while its textures are prepared. See [docs/NATIVE_GPU.md](docs/NATIVE_GPU.md). |

## Building the launcher

```bash
cmake -S . -B build -G Ninja && cmake --build build
./build/app/kartrecomp
```

You need Qt 6 (Widgets) and CMake 3.22 or newer.

## Repository layout

```
app/            Qt6 launcher source
patches/suyu/   our changes to the suyu fork (static explicit content + DLC, host hooks + NVN tracer)
tools/          helper scripts (NSP version reader, NSP packer, static registry generator)
docs/           pipeline, roadmap, notes
third_party/    submodules (added later; see docs/PIPELINE.md)
local/          gitignored: your game build, keys, saves. Never committed.
```
