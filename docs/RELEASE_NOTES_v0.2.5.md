# Racing 8 DX Recomp v0.2.5

You bring your own game files; nothing from the game is in this release. No JIT anywhere.

Download: **Linux** `Racing_8_DX_Recomp-x86_64.AppImage` (with a `.sha256`). The Windows zip follows from the Windows build.

**Your game is rebuilt once** the first time you press Play or Build. The builder makes faster code ("fast code" below) and adds the
points the ultrawide option needs.

## New

- **Online play** (new *Online play* tab). Play the game's **Wireless Play** with people anywhere, through a multiplayer room (a small
  relay server):
  - Tick **Enable online play**, then either **Host a room on this computer** or **Join a room** (address, port, password).
  - The host gets its address and a **Copy join details** button.
  - In the game, one player chooses *Create Room* in Wireless Play; the others see it in the lobby.
  - Rooms run by suyu/yuzu-style room servers work too.
  - Players outside your network need the host's UDP port (24872 by default) forwarded.
  - New in this version: tested here with a hosted room (Create Room works), not yet with several players.
- **Launcher in the game's 12 languages** (choose on the Setup page). Every launcher and build message is translated.
- **Ultrawide** (Display tab, *Picture shape*): 21:9 / 32:9 / automatic, for single-player races.
- **Frame rate tab**: 60 / 120 / 180 / 240 fps through frame generation (the game itself always runs 60 steps a second).
- **Mii preview**: the Mii editor shows the Mii (needs your firmware).
- **Setup without keys or firmware**: a *decrypted files* mode that takes your game's ExeFS folder plus its RomFS folder or image.
- **Faster game builds**: only the parts whose code or options changed are compiled again.

## Faster and lighter

- **Fast code**: about 30% less CPU time on the game's main thread (direct memory access, chained branches, native calls).
- **Native engine**: the game no longer runs on an emulated system underneath. Its services (controllers, audio, files and saves,
  graphics driver, display, accounts, settings, fonts, Miis...) are answered by the engine's own runtime. Waits, locks and threads are
  native, and the emulated GPU is gone. About 1 GB less memory, quicker start-up, and the same 60 fps in 1P and 4P.

## Fixes

- **Crash going into the next race** (after finishing a race of a Grand Prix). Checked with whole scripted Grand Prix runs: four
  races and the trophy, no errors.
- **Wireless Play stopped the game** (error 2021-0007) as soon as it was opened.
- **Online Play froze on "A communication error has occurred"** with the new engine. Nintendo's online service cannot be used by a
  recompiled game, so the game now gets a clean "no connection" answer and returns to the main menu. To play over the internet, use
  Wireless Play with online play enabled (above).
- Closing the game could crash.

## For testers: the new kernel (experimental, Linux)

The game can also run on the engine's own kernel instead of the emulated one, the last emulated part. 1P, 4P and the keyless setup
race at 60 fps in testing. It is off by default. To try it, add this line to `~/.config/kartrecomp/launch.env`:

    KARTRECOMP_NATIVE_KERNEL=1
