# Racing 8 DX Recomp v0.2.4

You bring your own game files; nothing from the game is in this release. No JIT anywhere.

Downloads: **Linux** `Racing_8_DX_Recomp-x86_64.AppImage`, **Windows** `KartRecomp-windows-x64.zip`. Each has a `.sha256`. **A game built with
0.2.3 is rebuilt once** the first time you press Play or Build (the builder now compiles the hot code at a higher optimisation level).

## New

- **Controllers, like Eden's** (Controllers tab): each player picks what it plays on (found automatically); every button and stick is
  listed with your controller's own name for it ("LB", "Cross", ...) and what it does in game. **Change** opens "Press the button for
  in-game X" and takes your next press. **Swap A/B and X/Y** for Xbox/PlayStation layouts, **Standard layout**, and "Give controllers to
  players automatically" (on by default). Layouts belong to the controller: each one keeps its own, whichever player gets it.
- **In-game Controls page** (F10 or L3+R3, then Controls): pick a player, see every button, rebind by pressing, swap A/B-X/Y.
- **`-f` / `--fullscreen`** on the command line starts the game fullscreen, with or without `--nolauncher`
  (e.g. `Racing_8_DX_Recomp-x86_64.AppImage --nolauncher -f`, or `kartrecomp.exe --nolauncher --fullscreen` for Steam).
- **Quit**: "Quit game" in the in-game menu; holding B for 5 seconds on the "press L+R" title screen closes the game.
- **60 fps split-screen** (Setup page option, off by default): in 3-4 player races the game normally draws half of the views each frame
  (30 fps per player); with the option every view is drawn every frame. Built into the game when it is compiled (no JIT); switching it
  rebuilds only the part that changes. Needs a fast CPU: when the PC cannot keep 60, the race slows down.
- **Split-screen fixes**: 2-4 player views were black with HDR, a render scale or an upscaler on.
- **Graphics quality Low/High** (was handheld/docked) no longer changes the resolution.
- **Frame generation**: built-in 2x/3x/4x (120/180/240 fps), and it now works with a 150%/200% render scale.
- **Sharper motion**: better motion vectors for your own kart (less smearing with FSR 3 and frame generation).
- **Saves and Miis in plain folders**: `saves` and `miis` in the data folder (`~/.local/share/kartrecomp`, or `userdata` beside the exe on
  Windows); the launcher has "Open saves folder" and "Open Miis folder". Your old save is copied over once and kept.
- **Mii editor** (Miis tab).
- **Menu mods on 4.0.1** (for example Vector): the game no longer stops at the flag screen; the launcher merges the mod's menu changes
  into the 4.0.1 menus.
- **Faster**: 4.0.1 now runs as fast as 3.0.4; less memory use; shaders adapt to GPUs without 64-bit floats (Intel Xe).

## Known issues

- 60 fps split-screen costs about 30% more CPU/render time in 3-4 player races (about 47 fps on the test PC, where the plain game holds 60).
- Updates other than 3.0.4 and 4.0.1 are not supported. Bloom is wrong at 150% and 200% render scale. DLSS on Linux has not been run on
  an NVIDIA GPU yet.

## Requirements

A CPU with AVX2/BMI2/FMA (Intel Haswell / AMD Zen or newer). Windows 10/11 64-bit, or Linux with glibc 2.35+ and Vulkan. Your own base
game, the 3.0.4 or 4.0.1 update, firmware, and `prod.keys` from that firmware version or newer. Mods stay data-only.
