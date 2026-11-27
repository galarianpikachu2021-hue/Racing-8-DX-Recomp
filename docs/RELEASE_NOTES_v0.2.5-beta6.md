# NOT A FULL RELEASE: I CANNOT GUARANTEE IT IS FULLY FUNCTIONAL OR STABLE

If you report a bug, tag it with "BETA Issue" and say it's beta 6. After updating, run Setup again: the game code is built differently
now (the launcher tells you).

## Faster (mostly for the Steam Deck and other 4-core PCs)
- **The game's graphics calls no longer detour through the engine's dispatcher.** About 4,300 calls a frame used to leave the game's code,
  pass the dispatcher and come back; they are now handled where they are made.
- **Buffer copies for the renderer are spread over more cores.** The game thread that hands each frame to the renderer (which the main
  thread waits for) copied several MB a frame alone; the copies now run on it and two helper threads. Submit time -15% here.
- **The game's memcpy / memset / string functions run as native code** (they were opt-in; with the detour gone they are now faster than
  the game's own versions). Large zero fills still give their memory back.
- **First game function rewritten natively:** the bounding sphere of a moving model (characters, karts, items: it runs for every racer,
  so it costs more with more computer racers). It only answers when the result is guaranteed identical to the game's own code and
  otherwise lets the game's code run; checked identical over millions of calls on 4.0.1 and 3.0.4.
- **A floating-point step the characters' skeletons use often (reciprocal square root estimate) is a table lookup now**, with results
  identical to before for every possible input.

On this PC (4 cores, uncapped race) the main game thread needs about 2% less CPU and the frame hand-off 15% less time. The detour and
copy changes should matter more on the Steam Deck, whose CPU paid more for both; I could not measure that here.

## Switches (in `launch.env`, if you suspect one of the changes)
- `KARTRECOMP_XM_HOOKS=0`: graphics calls through the dispatcher again.
- `KARTRECOMP_SNAP_THREADS=0`: buffer copies on one thread again.
- `KARTRECOMP_NATIVE_LIBC=0`: the game's own memcpy / memset / string code.
- `KARTRECOMP_NATIVE_FNS=0`: the game's own bounding-sphere code.
- `KARTRECOMP_NO_XM=1`: no direct calls into the system library at all (beta 5 change).

## Known issues
- A rare freeze at the menu movies at start-up is not found yet.
- No DLSS / NVIDIA test here: #42 and #43 need a report from an NVIDIA PC.
- CTGP DX on Windows: L / R on the cup screen shows no new cups (#28).
- At 33% render scale, fine texture detail on moving characters is grainy.

## Downloads
- `Racing_8_DX_Recomp-x86_64.AppImage` (Linux, x86-64-v3 CPU, Vulkan 1.3 GPU) and its `.sha256`. Engine `9fb61db425-abi6`: run Setup
  again after updating (the game refuses modules built for an older engine and tells you so).
- No Windows build in this beta.
