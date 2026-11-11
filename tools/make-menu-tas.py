#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Writes a suyu TAS script that drives MK8D from boot into a Grand Prix race.

It taps A through the title and menus (default picks: single player, Grand Prix,
50cc, the highlighted racer and kart, the first cup), then accelerates for the rest
of the run, weaving the stick and using items now and then. It's useful for
testing renderers on menus, the race HUD and item effects without a person at the
controls.

    tools/make-menu-tas.py local/game/user/tas/script0-1.txt [--smart-steering] [--centre-stick] [--effects] [--cup=N]
    (cd local/game && ./play.sh -t)

Races now always switch Smart Steering on from the pause menu (use --weave for the old unassisted weaving; time trials have
no antenna). --smart-steering presses + once on the character select screen, which TOGGLES
Smart Steering (the antenna) - the game saves it, so use it once to switch it on,
not on every run. --centre-stick keeps the stick centred and switches Smart
Steering on for race 1 from the pause menu (+, L, +) right after "GO".

--effects (implies --centre-stick) adds a drift every 7 s from frame 3000: hop with R, hold it
while steering 2.5 s, alternating left and right, then release for a mini-turbo. That, plus the
item use already in the script, makes the effect-heavy frames (boost flames, sparks) that the plain
race lacks.

--time-trial picks Time Trials instead of Grand Prix (the rest of the menu taps are the same; see docs/BENCHMARK.md).

--cup=N picks the Nth cup to the right of the Mushroom Cup (the first race then differs).

Format: one line per frame with input: `frame buttons lstick rstick`, sticks as
`x;y` in -32767..32767. Frames without a line have no input.
"""
import math
import sys


def main() -> int:
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    if len(args) != 1:
        print(__doc__)
        return 2
    toggle_steering = "--smart-steering" in sys.argv
    effects = "--effects" in sys.argv
    time_trial = "--time-trial" in sys.argv
    cup = next((int(a.split("=", 1)[1]) for a in sys.argv if a.startswith("--cup=")), 0)
    # --players=N (2-4): split-screen Grand Prix. The main menu takes input from about frame 510: the A at 520 is dropped, Down at
    # 530 picks Multiplayer and N-2 Right presses the player count. Players 2..N get script0-<n>.txt next to the given file with the
    # same menu taps and the accelerator held in the race (player 2 must be connected in sdl2-config.ini, and so on).
    players = next((int(a.split("=", 1)[1]) for a in sys.argv if a.startswith("--players=")), 1)
    # Races always use Smart Steering (the antenna): the stick stays centred and the pause-menu toggle below switches it on once the
    # countdown is over (verified on Mario Kart Stadium and Mario Circuit: the pause panel reads OFF until the L press, ON after).
    # Time trials have no antenna. --weave brings back the old sine-wave steering without it.
    weave = "--weave" in sys.argv
    centre_stick = not weave and not time_trial
    lines = []
    race_start = 2300 if centre_stick else 3400
    # Title and menus: hold A for 4 frames every second.
    menu_taps = [f for f in range(400, race_start, 60) if not (players > 1 and f == 520)]
    if players > 1:
        for k in range(4):
            lines.append(f"{530 + k} KEY_DDOWN 0;0 0;0")
        for i in range(players - 2):
            for k in range(4):
                lines.append(f"{545 + 12 * i + k} KEY_DRIGHT 0;0 0;0")
    for f in menu_taps:
        for k in range(4):
            lines.append(f"{f + k} KEY_A 0;0 0;0")
        # Character select is on screen around frame 1000.
        if toggle_steering and f == 1000:
            for k in range(30, 34):
                lines.append(f"{f + k} KEY_PLUS 0;0 0;0")
    # --time-trial: the mode menu (Grand Prix, Time Trials, VS Race, Battle) is up from about frame 600 (the first menu A tap is at 640 or later); one Down moves to Time Trials.
    if time_trial:
        for k in range(4):
            lines.append(f"{615 + k} KEY_DDOWN 0;0 0;0")
    # --cup=N: the cup-select screen is up around frame 1260 and the A tap at 1300 confirms it, so
    # N right-presses in between move N cups along (1 = Flower Cup, whose first course, Mario Circuit,
    # has an open sky; the default Mushroom Cup starts in the mostly enclosed Mario Kart Stadium).
    for i in range(cup):
        for k in range(4):
            lines.append(f"{1262 + 8 * i + k} KEY_DRIGHT 0;0 0;0")
    # Smart Steering resets each race and is toggled from the pause menu (L), which
    # only opens once the race has started: + (pause), L, + (resume) after "GO",
    # with the accelerator released (A would pick "Continue"). "GO" is around frame 2325 on a normal
    # load; the presses are 375 frames later (menu open at 2700, L held 10 frames at 2800, resume at 2920 so the toggle always registers first) so a slower load still lands them after the countdown.
    # 2026-11-11: loads got faster ("GO" at frame 2220 with the v0.2.3 engine) and the game's own Auto-accelerate is on, so the
    # kart drives off at GO by itself and reached the first wall (about 2600) before the old 2700 toggle. Pause at 2300 instead,
    # still on the opening straight; 80 frames of margin if a load is slower. While paused the kart does not move.
    pause_window = range(2290, 2480) if centre_stick else range(0)
    pause_toggle = {2300: "KEY_PLUS", 2370: "KEY_L", 2450: "KEY_PLUS"}
    # Race: accelerate, weave gently (8 s period), use an item every 5 s.
    for f in range(race_start, 12000):
        if f in pause_window:
            button = next((b for start, b in pause_toggle.items() if start <= f < start + (10 if b == "KEY_L" else 4)), None)
            if button:
                lines.append(f"{f} {button} 0;0 0;0")
            continue
        x = 0 if centre_stick else int(12000 * math.sin(f / 480 * 2 * math.pi))
        buttons = "KEY_A;KEY_L" if f % 300 < 4 else "KEY_A"
        if effects and f >= 3000:
            n, k = divmod(f - 3000, 420)
            if k < 150:
                buttons += ";KEY_R"
                x = (-24000 if n % 2 == 0 else 24000) if k >= 8 else 0
        lines.append(f"{f} {buttons} {x};0 0;0")
    # --hz=N: the game runs at N frames per second (KARTRECOMP_VSYNC_HZ); every input frame is scaled by N/60 so the inputs land at
    # the same moments of time (the TAS advances one step per game frame). For the true frame rate experiments.
    hz = next((int(a.split("=", 1)[1]) for a in sys.argv if a.startswith("--hz=")), 60)
    if hz != 60:
        scaled = {}
        for line in lines:
            f, rest = line.split(" ", 1)
            base = int(f)
            # Keep held inputs held: a run of consecutive frames becomes a run of N/60 times as many frames.
            for k in range(round(base * hz / 60), round((base + 1) * hz / 60)):
                scaled.setdefault(k, rest)
        lines = [f"{k} {v}" for k, v in scaled.items()]
    lines.sort(key=lambda line: int(line.split()[0]))
    with open(args[0], "w") as out:
        out.write("\n".join(lines) + "\n")
    if players > 1:
        import os
        base = os.path.join(os.path.dirname(os.path.abspath(args[0])), "script0-{}.txt")
        other = [f"{f + k} KEY_A 0;0 0;0" for f in menu_taps for k in range(4)]
        other += [f"{f} KEY_A 0;0 0;0" for f in range(race_start, 12000)]
        other.sort(key=lambda line: int(line.split()[0]))
        for n in range(2, players + 1):
            with open(base.format(n), "w") as out:
                out.write("\n".join(other) + "\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
