# NOT A FULL RELEASE: I CANNOT GUARANTEE IT IS FULLY FUNCTIONAL OR STABLE

If you report a bug, tag it with "BETA Issue" and say it's beta 4. After updating, run Setup again: the engine changed a lot.

## Fixed
- **Upscaling below 100% render scale:**
  - The race HUD (placement, map, laps, coins), the results list and the "Next Race" menu were drawn at the low render scale on
    some courses and looked pixelated. They are drawn at full output resolution again.
  - The end-of-Grand-Prix "Congratulations!" screen: only the trophy is upscaled now, and the results board is sharp.
  - The player's kart and character looked blocky with FSR 3: its motion vectors were guesses that FSR 3 rejected, so it showed
    the raw low-resolution pixels there. See "Per-object motion vectors" below.
- **Camera jitter shook the picture** (the whole screen, the results board). It now only ever moves the 3D scene, where the
  upscaler resolves it.
- **Crash on start-up with render scale above 100%** (150% / 200%).
- **Crash when quitting the game** (Linux).

## New
- **Per-object motion vectors:** every kart, character and item now gets exact motion from the game's own vertex shaders, so
  FSR 3 keeps its detail on moving objects instead of falling back to blocky pixels. Needs a GPU with the Vulkan extensions
  `VK_EXT_transform_feedback` and `VK_KHR_fragment_shader_barycentric` (current AMD and NVIDIA GPUs have them); without them
  the old vectors are used. Costs about 1 ms of GPU time per frame. `KARTRECOMP_OBJMV=0` in `launch.env` turns it off.
- **Camera jitter is automatic:** on whenever FSR 3 or DLSS is selected, off otherwise. The menu row is gone.
- **CTGP Deluxe:**
  - the in-race music switch (L/R during the race intro) and the now-playing banner;
  - the extra cups on every cup page (Grand Prix, VS, Time Trials);
  - the speedometer.
- **Linux: the game runs on our own runtime kernel.** About 25,000 lines of the emulated Switch kernel are no longer part of the
  Linux engine. You should notice nothing, except fewer threads.
- Windows: the Windows build gets the same changes; the runtime kernel stays switched off there until it has been tested.

## Known issues
- At 33% render scale, fine texture detail on moving characters is still grainy (FSR 3 rebuilds 9 screen pixels from 1).
- The CTGP Settings page and title screen are not recreated yet.
