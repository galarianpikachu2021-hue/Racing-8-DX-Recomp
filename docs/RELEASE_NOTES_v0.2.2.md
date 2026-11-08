# Racing 8 DX Recomp v0.2.2

You bring your own game files; nothing from the game is in this release. No JIT anywhere.

Downloads: **Linux** `KartRecomp-Launcher-x86_64.AppImage`, **Windows** `KartRecomp-windows-x64.zip` (unzip the whole folder, run `kartrecomp.exe` from inside it). Each has a `.sha256`. **A game you already built with 0.2.1 keeps working**: no rebuild needed.

## Fixed

- **Online Play froze the game** (#7). Online play is not supported, so the game shows a "can't connect" error, and that error screen never closed. Now it closes and you are back at the main menu.
- **"CLI explicit content rejected: normal loader selected a different executable module set"** when pressing Play (#8). A mod with an `exefs` (code) folder, e.g. CTGP's Skyline loader, changed the game's modules. Code mods cannot run in the recompiled build, so their `exefs` part is now ignored with a warning in the log; the mod's data (romfs) still applies.
- **Build crash `0xc0000094` with firmware 23.x** (#2, #8). Firmware newer than 22.5 is not supported yet. The Setup page now refuses a firmware folder or zip whose name says it is newer than 22.5, and if the test boot crashes this way the message says to use firmware 22.5.0 or older.
- **"Cannot read this file: the exporter gave no answer"** (#9). Warnings printed around the exporter's answer (seen with newer `prod.keys`) no longer hide it; when there really is no answer, the message shows what the exporter printed.
- The launcher reported version 0.1.0 and kept offering an update; it now knows its own version.

## Known issues

- Firmware newer than 22.5 (23.x) crashes the game's start: use 22.5.0 or older.
- Bloom is wrong at 150% and 200% render scale (supersampling).
- Windows: an occasional crash when closing the game.
- Linux AppImage: built on a very recent distro (glibc 2.44), so it starts only on new distros for now.
- DLSS on Linux (launcher: Enhancements, "Set up NVIDIA DLSS") has not been run on an NVIDIA GPU yet.

## Requirements

A CPU with AVX2/BMI2/FMA (Intel Haswell / AMD Zen or newer). Windows 10/11 64-bit, or a recent Linux with Vulkan. Your own base game, the 3.0.4 update, `prod.keys` and firmware 22.5.0 or older. Mods stay data-only.
