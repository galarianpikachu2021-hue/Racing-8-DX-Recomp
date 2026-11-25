# NOT A FULL RELEASE: I CANNOT GUARANTEE IT IS FULLY FUNCTIONAL OR STABLE

If you report a bug, tag it with "BETA Issue" and say it's beta 5. After updating, run Setup again: the game code is built differently now.

## Faster
- **The game's own code needs about 40% less CPU** (main thread -41%, worker threads -39% to -48%, 12 racers on Mario Kart Stadium).
  The computer-controlled racers run there. Run Setup again to get it; the game build also takes less disk space.
- **The picture no longer waits for the GPU:** the renderer starts the next frame while the GPU finishes the last one, and hands each
  frame over in parts. 4K with FSR 3 and frame generation at a race start: 51 -> 60 fps. Helps most on weaker PCs.
- **Object motion vectors only for what moves:** Sydney Sprint holds 60 fps again.
- **Split-screen: the render scale works there too** (below 100%).

## New
- **Storage tab:** keep the game (game files, built game, mods, saves, build files) on an SD card or another drive, and build and play it
  from there. "Move everything there" copies first and removes the old files only when all of them have arrived; "Use it without moving"
  starts in the new place, for a device that is full. If the drive is not connected the launcher asks instead of starting over.
- **Two controller shortcuts** (Controllers tab, and Controls in the in-game menu), for every controller: open the in-game menu (still
  L3 + R3 unless you change it) and close the game (nothing unless you set it). One button or a combination.

## Fixed
- **Crash at the award ceremony on NVIDIA (#27):** a shader could be translated before the game had put its code in place (PR #50).
  Confirmed by its author on an RTX 3070 Ti.
- **CTGP DX: the track author line** at the start of a race said "WWWW..."; it shows the author now ("Nintendo" on the game's own tracks).
- **The game froze after your first cup win** (Linux): the staff credits movie needs the audio output service. It plays now.
- **The amiibo screen** answers (there is no reader, so the game waits for one).
- **Sky-High Sundae's sky on AMD:** coral-coloured slabs and lines in the sky are gone.
- **Invalid Vulkan use** found with the validation layer (layout changes inside a render pass, empty descriptor updates, the swapchain used
  from two threads with frame generation, and more). These may be behind the NVIDIA report #43: please test.
- FSR 3: grey "standing still" specks on the near road in the motion vectors.
- The speed limit row in the F10 menu did nothing; it works again.
- prod.keys with only master keys work in Setup (#34).
- Setup says why Play is greyed out when the game was built by another version, for example after copying it to another PC (#44).
- Windows: start-up hang (#45), direct memory (PR #48, off on PCs with under 12 GB), fast code without it (PR #46), lighter hook timing
  (PR #47); Wireless Play no longer drops after joining (PR #40). Thanks to DoubleLama and SnowdenDev.

## Known issues
- A rare freeze at the menu movies at start-up is not found yet.
- No DLSS / NVIDIA test here: #42 and #43 need a report from an NVIDIA PC.
- CTGP DX on Windows: L / R on the cup screen shows no new cups (#28).
- At 33% render scale, fine texture detail on moving characters is grainy.
