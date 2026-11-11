# Racing 8 DX Recomp

**Mario Kart 8 Deluxe, recompiled to run natively on your PC. Your own game, no emulator JIT, with modern upscaling and frame generation built in.**

Not affiliated with or endorsed by Nintendo. You supply your own legally obtained game dump, keys and firmware; this project ships none of them,
and no recompiled game code either (see [LEGAL.md](LEGAL.md)). (*KartRecomp* is the launcher's name inside the project.)

## Why it looks and runs better

| | |
|---|---|
| **Pick your resolution, natively** | Output at **720p, 1080p, 1440p or 4K**, switched live from the in-game menu (F10, or both stick buttons). The game's own 3D scene is drawn at the size you choose, the HUD stays razor sharp, and textures follow the resolution (per-resolution LOD, bloom scaling). |
| **Render scale 33% to 200%** | Draw the 3D scene below native and upscale it for speed, at native with anti-aliasing (**DLAA**), or **supersample up to 200%**. The HUD is always drawn at full output resolution, after the upscale. |
| **AMD FSR 3.1 upscaling** | Temporal FSR 3.1 (AMD FidelityFX SDK, Vulkan) fed by motion vectors we derive from the game's depth buffer and camera, plus an object layer for karts and items. Built-in **FSR 1** and adjustable sharpening too. |
| **FSR 3 frame generation, 2x** | AMD's optical-flow frame interpolation on the HUD-free 3D image, with the HUD laid over each generated frame so it never smears. Measured in a scripted race: the game keeps its 58 to 60 fps, the display shows double. (Half a frame of added latency, like any interpolation.) A built-in interpolator is the fallback. |
| **DLSS, experimental** | **DLSS upscaling and DLSS frame generation** for NVIDIA RTX GPUs, through NVIDIA's public NGX SDK, as plugins you build yourself (`plugins/dlss/`). It is written and compiles, but our development machine has an AMD GPU: **untested on real NVIDIA hardware so far**, so expect rough edges and please report what you see. |
| **HDR10 output** | True HDR with adjustable paper white and peak brightness, tonemapped by us from the game's HDR scene, HUD kept at a sensible level. |
| **Native renderer** | The game's own graphics calls are translated straight to Vulkan (no emulated GPU): 60 fps races, 4-player split-screen, a sub-pixel jitter and depth-based motion vector pipeline that temporal upscalers need. |
| **No JIT, ever** | Every instruction of the game runs as ahead-of-time compiled code. A missing block is a bug to fix, never a fallback to a JIT. |

Everything above is a row in the F10 menu. The launcher's Enhancements tab sets the starting values.

### Honest status of each piece

| Piece | State |
|---|---|
| Output resolution, render scale, DLAA, supersampling, FSR 1, sharpening | working, used daily |
| FSR 3.1 upscaling plugin, FSR 3 frame generation plugin | working on AMD (RDNA 4) / Vulkan; the SDK is fetched, never committed |
| Built-in frame interpolator | working |
| HDR10 output | working (needs the display in HDR mode) |
| **DLSS upscaling / DLSS frame generation** | Windows: tested by the maintainer on an NVIDIA PC (you build the plugin yourself). Linux: the launcher's **Set up NVIDIA DLSS** button builds it on your PC; **not yet run on an NVIDIA GPU on Linux** |
| **FSR 4.1.2** (Windows) | AMD's official Vulkan release (SDK 2.4.0, 2026-10-28): plugin in the Windows zip (AMD's signed DLLs, unmodified, with their licence), **not yet run on an RX 9000**; model select (FP8 / INT8 / lightweight INT8) in the in-game menu. See `docs/WINDOWS_PLUGINS.md` |
| Linux (AppImage, launcher with updater, mods) | working; built on Ubuntu 22.04, so it needs glibc 2.35+ (Ubuntu 22.04+, Fedora, SteamOS, Debian 12+) |
| Android (Snapdragon handhelds, including dual-screen) | in development (no document yet) |
| Windows | supports Windows 10/11 64-bit (portable zip, built locally with MSVC) |


## How it works

1. Open the launcher (the AppImage on Linux). Its **Setup** page asks for your own files: the base game, the 3.0.4 update, optional DLC,
   your `prod.keys` and your firmware. Each file is checked on the spot and a wrong one says why (an old 32-bit update, another game, a key
   missing).
2. **Build game** turns them into the game on your PC. The AppImage carries everything it needs: the prebuilt engine, the exporter that
   translates the game's AArch64 code into C, and a compiler. The tiered build (hot code optimised, cold code compiled fast) takes about
   5 minutes on an 8-thread PC; a test boot then checks that every instruction runs as recompiled code (no JIT, ever) before **Play**
   is enabled.
3. Nothing from the game is ever shipped: the translated code and your files stay in your own folders
   (`~/.local/share/kartrecomp`, `~/.config/kartrecomp`, `~/.cache/kartrecomp`).

4. **Play without the launcher** (TV, controller, Steam): start it with `--nolauncher`, e.g. `Racing_8_DX_Recomp-x86_64.AppImage --nolauncher`
   or `kartrecomp.exe --nolauncher` (add that as a non-Steam game). The game starts with your saved settings; the in-game menu (F10, or
   L3+R3 on a controller) has the graphics settings. The console language (the game's language) is on the launcher's Play tab.

The full recipe, including memory and time costs, is in [docs/PIPELINE.md](docs/PIPELINE.md).

## More features

| Feature | State |
|---|---|
| DLC via `content/dlc/*.nsp` | working |
| Mods (layered romfs data mods, character swaps, text merge between mods), per-mod switches | working |
| Controller auto-mapping (SDL3), 1 to 4 players | working (Steam Controller goes through SDL3 too, not yet tried on real hardware) |
| Saves, config and logs in a portable `user/` folder (or the XDG folders in the AppImage) | working |
| Updater: checks this repository's releases (a source checkout pulls and rebuilds, an AppImage replaces itself) | working |
| Console language (launcher, Play tab) | working |
| Start without the launcher (`--nolauncher`, for TV / Steam) | working |
| Online play | not supported (the game shows its "can't connect" error and returns to the menu); see the [roadmap](docs/ROADMAP.md) |
| Game versions | the **3.0.4** or **4.0.1** update; any firmware, with `prod.keys` from that firmware version or newer |
| Custom frame rates above 60 | planned (needs game-side patches, see the [roadmap](docs/ROADMAP.md)) |

## Building the launcher

```bash
cmake -S . -B build -G Ninja && cmake --build build
./build/app/kartrecomp
```

You need Qt 6 (Widgets, Network) and CMake 3.22 or newer.

Release builds: Linux `packaging/linux/build-release-ubuntu2204.sh` (podman, Ubuntu 22.04: engine, exporter, launcher and the AppImage);
Windows: built locally with Visual Studio 2022 and Qt 6.7 (steps in `COOPERATE.md` and `docs/HANDOFF-WINDOWS.md`). GitHub Actions only run by hand.

## Credits

Built on the suyu/Eden emulator code (GPL-3.0), AMD FidelityFX (MIT) for FSR 3.1 and frame generation, NVIDIA's public NGX SDK for the DLSS
plugin (its libraries are never redistributed). Not endorsed by any of them.

## Repository layout

```
app/            Qt6 launcher source
patches/suyu/   our changes to the suyu fork (static explicit content + DLC, host hooks, native renderer)
plugins/        FSR 3 and DLSS plugins (built against the vendors' SDKs, fetched at build time)
packaging/      AppImage packaging
tools/          helper scripts (recompiler, TAS and benchmark tools, packing)
docs/           pipeline, upscaler, native renderer, roadmap, notes
data/           the hot-block list (addresses only) for the fast build
local/          gitignored: your game build, keys, saves. Never committed.
```
