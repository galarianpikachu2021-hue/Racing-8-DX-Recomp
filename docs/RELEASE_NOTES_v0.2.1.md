# Racing 8 DX Recomp v0.2.1

You bring your own game files; nothing from the game is in this release. No JIT anywhere: the game is statically recompiled on your PC from your own files.

Downloads: **Linux** `KartRecomp-Launcher-x86_64.AppImage`, **Windows** `KartRecomp-windows-x64.zip` (unzip anywhere, run `kartrecomp.exe`; unsigned, so SmartScreen asks once). Each has a `.sha256`.

## What changed since 0.2.0

### Performance (both platforms)

- **Identical shader pipelines were rebuilt all through a race** (the pipeline key hashed state the pipeline never reads): a race reached 9,000 to 17,000 pipelines for about 2,000 programs. It now stays under 800. Less memory, a smaller cache file, fewer hitches.
- **Static vertex buffers are no longer copied every frame**, and pipelines are only looked for when something can be new. On a Windows test PC this moved frames within 17.5 ms from about 25% to about 65%, and frames over 20 ms from 55% to 7%. On Linux nearly every frame is on time (592-599 of 600 in a scripted race, including the race start).
- Windows: mimalloc replaces the slow default allocator in the engine (its licence text is in `engine\mimalloc-LICENSE.txt`); the engine keeps itself on the performance cores of hybrid Intel CPUs; the pipeline cache is saved from a background thread.

### Windows

- First Windows release: FSR 3.1 upscaling and FSR 3 frame generation, FSR 4 (AMD FSR SDK 2.4, Vulkan) with its lightweight models, and a **Model** row under Upscaler in the in-game menu (F10). DLSS is built by you locally and is never part of a release (NVIDIA's licence).
- F11 switches fullscreen on and off.
- Frame generation no longer shows part of the picture at output sizes above 1080p (the double-image flicker with every backend).
- A refused settings save (another program holding the file open) no longer ends the game.
- Crash reports on Windows: on a crash the engine writes the stack to `crash_stack.txt`; `Collect-Logs.cmd` also writes `system-info.txt` (CPU, GPU, driver, refresh rate, power plan).

### Both

- Menus and their 3D previews (character and kart select) draw at the full output size; the render scale and upscaler only apply to the 3D scene of a race.
- Fine diagonal hatching (shadow acne) on karts and characters is gone: shadow maps use a stronger depth offset.
- A call through a null function pointer in the game's camera code (hovering a modded driver in character select, and rarely at race load) no longer ends the game.

## Known issues

- **Bloom is wrong at 150% and 200% render scale** (supersampling). Use 100% or lower for now.
- Windows: an occasional crash when closing the game (a shutdown-order race on a controller thread); it does not touch the game while playing.
- Windows: FSR 4 frame generation and DLSS have not been tried on real GPUs of every kind; FSR 4 models are chosen by the SDK per GPU.
- Linux AppImage: built on a very recent distro (glibc 2.44), so it starts only on new distros for now.
- Games built with 0.2.0 or earlier are rebuilt once by the launcher (the engine version changed).

## Requirements

A CPU with AVX2/BMI2/FMA (Intel Haswell / AMD Zen or newer). Windows 11 64-bit, or a recent Linux with Vulkan. Mods stay data-only.
