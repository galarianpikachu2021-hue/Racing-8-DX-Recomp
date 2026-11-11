# Racing 8 DX Recomp v0.2.3

You bring your own game files; nothing from the game is in this release. No JIT anywhere.

Downloads: **Linux** `Racing_8_DX_Recomp-x86_64.AppImage` (renamed from `KartRecomp-Launcher-x86_64.AppImage`), **Windows** `KartRecomp-windows-x64.zip`. Each has a `.sha256`. **A game you already built with 0.2.1 or 0.2.2 keeps working**: no rebuild needed.

## New

- **Update 4.0.1 is supported** besides 3.0.4 (#3, #5, #19): pick either on the Setup page. 4.0.1 gets its own list of hot code (carried over from 3.0.4's), so it builds as fast as 3.0.4. Files named "v4.0.0" are usually the old 32-bit 3.0.3 update, which cannot be recompiled; the Setup page says so.
- **The AppImage runs on older and non-rolling distros** (#13, #15): everything in it is now built on Ubuntu 22.04, so it needs glibc 2.35 at most (Ubuntu 22.04+, Fedora, SteamOS, Debian 12 and newer). Before, it needed glibc 2.44, so the exporter could not start on Fedora 44 or SteamOS ("the exporter gave no answer").
- **Console language** (#11): the launcher's Play tab has a "Console language" choice; the game starts in that language.
- **Start the game without the launcher** (#17): `--nolauncher` (or `--play`), e.g. `Racing_8_DX_Recomp-x86_64.AppImage --nolauncher` or `kartrecomp.exe --nolauncher` as a non-Steam game. The game starts with your saved settings; the in-game menu (F10, L3+R3) has the graphics settings.
- The launcher is called "Racing 8 DX Recomp" everywhere (desktop entry, AppImage file name), and it no longer links OpenGL itself (it starts on systems without `libopengl0`).

## Known issues

- Updates other than 3.0.4 and 4.0.1 are not supported. The CTGP DX extra cups (an experimental hooked build) are 3.0.4 only.
- Firmware newer than 22.5 (23.x) crashes the game's start: use 22.5.0 or older.
- Windows: a divide-by-zero crash at start on some PCs (#8), being investigated.
- Bloom is wrong at 150% and 200% render scale. DLSS on Linux has not been run on an NVIDIA GPU yet.

## Requirements

A CPU with AVX2/BMI2/FMA (Intel Haswell / AMD Zen or newer). Windows 10/11 64-bit, or Linux with glibc 2.35+ and Vulkan. Your own base game, the 3.0.4 update, `prod.keys` and firmware 22.5.0 or older. Mods stay data-only.
