# True frame rates above 60 (research, started 2026-11-11, 4.0.1)

Goal (user): the game itself running above 60 (e.g. 90 real frames, then 2x frame generation = 180), besides frame generation
(built-in 2x/3x/4x = 120/180/240 from 60, done). Frame generation does not change what the game simulates; this does.

## Findings

- **Game-time constant.** `main` has exactly one f32 1/60 (`0x3C888889`, read-only data at main+0xed5c90, 4.0.1) and 19 code sites
  that load it (ADRP+LDR; none built with MOVZ/MOVK): c4d68, c4d74, c4d80, 176234, 1c2dc4, 20585c, 20d5c8, 20d5e0, 20d6cc,
  236b88, 262328, 2aeda0, 2b75a4, 306d74, 32bfd4, 37179c, 371d90, 37246c, adefb4. 60.0f appears 38 times. No f64 1/60.
- **Pacing.** The emulated display's vsync (`core/hle/service/vi/conductor.cpp`, FrameNs; now `KARTRECOMP_VSYNC_HZ` for experiments)
  is not the only cap: at 90 Hz vsync and real-time emulated clock the game still queued a frame every 16.7 ms. With the emulated
  clock unlimited (speed limit off) it ran at about 240 fps. So a second 60 Hz limit runs on emulated time.
- **Correction (same day):** the second cap was the conductor's own scheduling: `GetNextTicks()` hard-coded 60 Hz, so only the
  first vsync used the new rate. Fixed: with `KARTRECOMP_VSYNC_HZ=90` the game runs at about 85-90 fps. (A shell quoting bug in the
  experiment script had set the speed limit to "unlimited" in two runs that showed ~240 fps; ignore those.)
- **The game counts frames.** At 90 Hz the race countdown still takes exactly 60 frames per number (3 at frame 2190, 2 at 2250, 1 at
  2310, GO at 2370), as at 60 Hz: the whole game runs 1.5x fast at 90 Hz. Its simulation is frame-based, not time-based.
- **Where that limit is (wrong lead, kept for the record).** The 19.2 MHz tick value of 1/60 s (320000) appears as an instruction immediate in `subsdk0` (the NVN
  graphics driver bundled with the game) at 0x20e60, 0x330a8, 0x33dc0, 0x17a704, 0x183574, and in its data 3 times; 1/120 s and
  1/30 s too (present intervals 0/1/2). Very likely the driver's present-interval pacing.
- **Runtime patching.** `memscan.req` `write32 <addr> <value>` writes guest data in a running game (data only: the code is
  recompiled, and a write to guest code is caught by the code guard). Checked: the 1/60 constant was changed to 1/90 live.

## What it means

Running the game at 90 or 120 correctly needs every frame-counted timer and per-frame physics step in the game changed (countdowns,
item timers, animations, kart physics, AI, lap and race clocks, replays), not one constant. The 1/60 constant above is only one of
them. That is a large reverse-engineering job (likely hundreds of places in `main`) with a high risk of subtle errors (lap times,
time trials). Frame generation (built-in 2x/3x/4x) gives smooth motion at 120/180/240 without touching the simulation.

## Plan (if continued)

0. Pacing: done (`KARTRECOMP_VSYNC_HZ`, conductor fixed).
1. (Superseded) find which subsdk0 site paces `nvnQueuePresentTexture` (hook trace at present) and lift it to the target rate.
2. Build-time immediate patches: a table (module, address, old instruction, new instruction) applied by the exporter to the user's
   own code before recompiling, so the patched game stays fully static. Same mechanism for the 1/60 data constant (data patch).
3. Test at 90 and 120: race timer vs wall clock (time trial), kart speed, item timings, lap/finish times, replays, online off.
   Many systems may count frames instead of using the 1/60 step; those would run fast and need their own patches.
4. Then the launcher option: game frame rate 60/90/120 (with frame generation on top).

## 60 fps split-screen (3-4 players): investigated 2026-11-11, not done

The user's "60fps-splitscreen-patch.zip" is a Wii U patch (xdelta for Cemu's `Turbo.elf`); it does not apply to Deluxe. The Switch
equivalent is Eden's "60FPS in Split Screen" `3.0.3.pchtxt` (nsobid 6A85262F...): `main+0xbc0b1c` `bne` -> `b`.

- **Measured** (renderer log "split-screen views per frame", `vk_shadow_backend.cpp`): 4-player races present at 60 fps but draw
  only 2 of the 4 views per frame (600/600 frames), so each view runs at 30 fps. The game never changes the swap interval
  (`conductor.cpp` logs a change).
- **3.0.3** (`KARTRECOMP_EXPORT_NSO_ONLY=1 kartrecomp-export ... --update <3.0.3 nsp>`, then decompress): the patched function walks
  0x378-byte view entries and, for a view in mode 2 (byte +0x370), makes a copy image of the view (format 0x1a) and blits it; the
  patch makes it return at once.
- **4.0.1 counterpart**: `main+0xafee4c` (0x410-byte entries, mode byte +0x408, `cmp w8,#2; b.ne 0xaff094` at 0xafee94/0xafee98).
  The modes are set once per frame by `main+0xafecc8` (reached through the virtual thunk `main+0x6b380c`, object = owner+0x250):
  frame counter byte +0x18, group count byte +0x19; group count < 2 or view slot (+0x409) == -1 -> mode 0 (drawn every frame);
  `counter % groups == slot` -> mode 2 (drawn, copy kept); else mode 1 (the copy is shown). The branch `b.lo 0xafed1c` at
  `0xafece8` is the switch.
- **Why it isn't a runtime toggle**: host hooks only run when execution comes back to the host dispatcher. In the 4.0.1 build,
  calls inside `main` (direct and indirect) are resolved in the generated code, so hooks at 0xafee9c, 0xafecec or the thunk
  0x6b380c never ran (`KARTRECOMP_HOOK_TRACE` saw no hits in 4-player races).
- **Next step**: build-time code patch. The exporter applies (address, old word, new word) to the user's own `main` before
  translating, e.g. `0xafece8: b.lo 0xafed1c` -> `b 0xafed1c` (every view mode 0), as a launcher option that rebuilds the game.
  Then measure CPU cost (twice the 3D work per frame in 3-4P) and check the picture.
