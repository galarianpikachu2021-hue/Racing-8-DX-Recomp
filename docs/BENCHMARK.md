# Replaying your own run as a benchmark

1. Record: start the game with `KARTRECOMP_RECORD=1` (records from the first frame, so the menus and every race replay the
   same way), or switch "Record my inputs" on in the in-game menu (F10) at any moment; a red REC shows while it records.
   Stopping the recording (the menu row, or closing the game) writes
   `local/game/user/tas/recordings/run-<date>-<time>.txt` in the TAS script format (frame, buttons, left stick, right stick).
2. Replay it in one renderer: copy it to `local/game/user/tas/script0-1.txt` and run `./play.sh -t`.
3. Compare renderers: `tools/replay-benchmark.py <recording.txt> [--modes emu,native] [--env KEY=VAL ...]` runs the same
   recording through suyu's own renderer and the native one, and prints average fps, median, 1% low and the seconds
   under 55 fps for each. It restores your previous script0-1.txt afterwards. Close the game first.

Limits: the recording steps once per presented frame, so a recording made from boot replays the same game as long as the
game itself is deterministic; if it uses random numbers the runs drift apart over time (compare average fps, not
positions). Recorded keyboard/controller input is player 1's state as the game saw it (nothing is recorded while the
in-game menu is open, because the game gets neutral input then).
