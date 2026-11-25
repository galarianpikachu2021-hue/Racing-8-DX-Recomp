# NOT A FULL RELEASE: I CANNOT GUARANTEE IT IS FULLY FUNCTIONAL OR STABLE

If you report a bug, tag it with "BETA Issue" and say it's beta 5. After updating, run Setup again: the game code is built differently now.

## Faster
- **The game's own code needs about 40% less CPU** (main thread -41%, worker threads -39% to -48%, 12 racers on Mario Kart Stadium).
  The computer-controlled racers run there. Run Setup again to get it; the game build also takes less disk space.
- **The picture no longer waits for the GPU:** the renderer starts the next frame while the GPU finishes the last one, and hands each
  frame over in parts. 4K with FSR 3 and frame generation at a race start: 51 -> 60 fps. Helps most on weaker PCs.
- **Object motion vectors only for what moves:** Sydney Sprint holds 60 fps again.
- **Split-screen: the render scale works there too** (below 100%).

## Fixed
- **The game froze after your first cup win** (Linux): the staff credits movie needs the audio output service. It plays now.
- **The amiibo screen** answers (there is no reader, so the game waits for one).
- **Sky-High Sundae's sky on AMD:** coral-coloured slabs and lines in the sky are gone.
- **Invalid Vulkan use** found with the validation layer (layout changes inside a render pass, empty descriptor updates, the swapchain used
  from two threads with frame generation, and more). These may be behind the NVIDIA reports #27 and #43: please test.
- FSR 3: grey "standing still" specks on the near road in the motion vectors.
- The speed limit row in the F10 menu did nothing; it works again.
- prod.keys with only master keys work in Setup (#34).
- Setup says why Play is greyed out when the game was built by another version, for example after copying it to another PC (#44).
- Windows: start-up hang (#45), direct memory (PR #48, off on PCs with under 12 GB), fast code without it (PR #46), lighter hook timing
  (PR #47); Wireless Play no longer drops after joining (PR #40). Thanks to DoubleLama and SnowdenDev.

## Known issues
- A rare freeze at the menu movies at start-up is not found yet.
- No DLSS / NVIDIA test here: #27, #42 and #43 need a report from an NVIDIA PC.
- CTGP DX on Windows: L / R on the cup screen shows no new cups (#28).
- At 33% render scale, fine texture detail on moving characters is grainy.
