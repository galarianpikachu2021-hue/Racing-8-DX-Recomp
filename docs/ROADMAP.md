# Roadmap

Status as of v0.2.3 (2026-11). Done items are kept short; open items say what is known.

## Done

- **Launcher** (Qt 6): Setup (build the game from your own NSPs, keys, firmware), Play, Display, Frame rate, Enhancements, Controllers,
  Miis, DLC and Mods tabs; console language; updater; `--nolauncher` to start the game directly.
- **One-click build**: version and key checks on every file, a resumable tiered build (about 5 minutes on 8 threads), a test boot that
  proves every instruction runs recompiled (no JIT, ever).
- **Native renderer** (NVN to Vulkan, no emulated GPU), 720p to 4K output, render scale 33-200%, DLAA, FSR 1, FSR 3.1, FSR 3 frame
  generation, built-in frame interpolation, HDR10, motion vectors, 1-4 player split-screen, in-game menu (F10 / L3+R3).
- **Windows** (v0.2.1): portable zip, FSR 3/FSR 4 plugins, DLSS built by the user, mimalloc.
- **DLSS on Linux** (v0.2.1): the launcher builds the plugin on the user's PC; nothing of NVIDIA's ships.
- **Older Linux distros** (v0.2.3): the AppImage is built on Ubuntu 22.04 and needs glibc 2.35 at most, which covers distros on 2.43
  (Fedora 44 and similar) and SteamOS (`packaging/linux/build-release-ubuntu2204.sh`).
- **Shader pipelines** (v0.2.1): pipeline count stays flat during a race (was 9,000-17,000 duplicates); static vertex buffers are not
  re-copied each frame.

## Online Play
- Figure out the network backend with the suyu engine (today the game's online menu fails to connect and returns to the main menu).
- Figure out the server side: local (LAN / self-hosted) or a public service; ask the devs of the existing community servers before
  building anything.

## More updates
- Done in v0.2.3: **4.0.1** (`tools/recomp/map_hot_list.py` carries the hot list over; it exports, builds and boots fully static).
- Files named "4.0.0" are the 32-bit 3.0.3 update: it needs an AArch32 recompiler (large), not planned yet.
- 3.0.4-only so far: the CTGP DX extra-cup hooks (hooked build) use 3.0.4 addresses.

## Firmware
- Done in v0.2.3: the start-up crash was keys older than the firmware (a missing key-area key crashed the AES layer); the engine now stops
  with a clear message instead, and firmware 23.0.0 with matching keys runs.

## Performance
- Windows: frame pacing on some PCs (#14, #20): shader-compile stalls at load, CPU time in the recompiled code.
- Split-screen with 2+ players drops to about 30 fps on some PCs.
- Load-time pipeline stalls (about 3 s at startup, 220-400 ms at race load).

## Later
- Custom frame rates above 60 (the simulation is tied to 60 Hz; needs game-side patches generated from the user's own game).
- Mii editor (CharInfo records with correct CRCs in the system save).
- More CTGP DX features (cup-screen rows, speedometer; the pack's own code is 32-bit and cannot run in this build).
- Android (Snapdragon handhelds, dual-screen): the renderer and shader translation are backend-neutral.
- Bloom at 150%/200% render scale.

## Open technical issues
- Windows: divide-by-zero crash at start on some PCs (#8, `mk8d-engine.exe+0x2e00eb`).
- An intermittent camera-table null call at race load (recovered since v0.2.1, cause unknown).
- NAND-installed update boot hang in suyu-cmd (see PIPELINE.md); the launcher always passes the explicit base + update pair instead.
