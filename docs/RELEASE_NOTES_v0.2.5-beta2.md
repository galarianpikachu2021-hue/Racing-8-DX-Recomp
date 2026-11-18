# NOT A FULL RELEASE: I CANNOT GUARANTEE IT IS FULLY FUNCTIONAL OR STABLE

Linux only for now (Windows is coming). If you report a bug, tag it with "BETA Issue" and say it's beta 2.

## Fixed
- **Setup failing at the end with "never presented frames"**: the build was fine, but the test boot at the end of Setup never
  reported frames, so every build was rejected. Setup now accepts a build that reaches the game's main loop and keeps running.
  If you hit this with beta 1 or v0.2.4, just run Setup again with this version.
- A DLC file that can't be read no longer stops the game (#32).
- Online rooms: traffic is reliable again (an Eden player on a Steam Deck crashed on connect).

## New
- **CTGP Deluxe, recreated for 3.0.4 and 4.0.1** (#28): import your own CTGP DX 1.1.1 zip in the Mods tab, then build the game
  in Setup again. CTGP's 16 cups get their own columns on the Grand Prix cup screen (press L/R), with the 64 tracks, names,
  pictures and CTGP's music. CTGP settings are in the F10 menu: race music (CTGPDX OST / Author's Pick / None), race timer,
  speedometer, and online mode (stays "Vanilla players" unless you change it). Not done yet: the in-race music switch, CTGP's
  Settings page and title screen, the extra cups on the VS/Time Trial cup screens. Nothing from CTGP is included: you need
  your own copy.
- 5.1 surround audio (Play tab: Audio output).
- Profile name for your player (launcher + game).
- Steam Deck profile (`--steamdeck`, or automatic in Steam's Deck mode).
- Controllers take Player 1, 2, 3, 4 in the order you plug them in; a controller plugged in while playing takes Player 1 from
  the keyboard.
- Setup option to delete the build files afterwards (saves about 6 GB).
- The Mods tab tells you when the game is running and locks the checkboxes.
