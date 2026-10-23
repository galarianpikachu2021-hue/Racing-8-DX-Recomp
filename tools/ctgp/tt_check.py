#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Loads every converted CTGP DX track in a Time Trial (scripted menus) and keeps a screenshot of each, to check that the course loads.

    tt_check.py <game dir (local/game)> <out dir> [cup first] [cup last]

Run it with the game closed. It backs up and restores user/config and the TAS script itself.
"""
import glob
import os
import shutil
import subprocess
import sys

game, out = sys.argv[1], sys.argv[2]
first = int(sys.argv[3]) if len(sys.argv) > 3 else 0
last = int(sys.argv[4]) if len(sys.argv) > 4 else 11
cups_only = len(sys.argv) > 5 and sys.argv[5] == "cups"
only_race = int(sys.argv[6]) if len(sys.argv) > 6 else None
here = os.path.dirname(os.path.abspath(__file__))
tool = os.path.join(here, "..", "make-menu-tas.py")
script = os.path.join(game, "user", "tas", "script0-1.txt")
cfg = os.path.join(game, "user", "config")
backup = os.path.join(out, "_backup")
os.makedirs(backup, exist_ok=True)
shutil.copy(script, os.path.join(backup, "script0-1.txt"))
for f in ("sdl2-config.ini", "kartrecomp_overlay.ini"):
    shutil.copy(os.path.join(cfg, f), os.path.join(backup, f))


def tas(cup, race):
    subprocess.run([sys.executable, tool, script, "--time-trial"], check=True, stdout=subprocess.DEVNULL)
    lines = [l for l in open(script).read().splitlines() if not 1230 <= int(l.split()[0]) <= 1700]
    used = {int(l.split()[0]) for l in lines}
    extra = []

    def press(frame, button):
        for k in range(4):
            if frame + k not in used:
                extra.append(f"{frame + k} {button} 0;0 0;0")

    # The cup screen is up from about frame 1200: move the cursor to the cup (16 frames per press: faster ones get lost), A, then to the
    # course, A, then A twice more (Solo Race, Start Time Trial).
    col, row = cup % 6, cup // 6
    t = 1262
    for _ in range(col):
        press(t, "KEY_DRIGHT")
        t += 16
    for _ in range(row):
        press(t, "KEY_DDOWN")
        t += 16
    press(1400, "KEY_A")
    for i in range(race):
        press(1440 + 16 * i, "KEY_DRIGHT")
    press(1530, "KEY_A")
    press(1590, "KEY_A")
    press(1650, "KEY_A")
    allv = lines + extra
    allv.sort(key=lambda l: int(l.split()[0]))
    open(script, "w").write("\n".join(allv) + "\n")


try:
    for cup in range(first, last + 1):
        for race in ([only_race] if only_race is not None else range(1 if cups_only else 4)):
            name = f"cup{cup:02d}_race{race}"
            tas(cup, race)
            shutil.rmtree(os.path.join(game, "user/log/native_dump/shots"), ignore_errors=True)
            env = dict(os.environ, KARTRECOMP_NATIVE_SHOTS="150", KARTRECOMP_NATIVE_GPU="native", KARTRECOMP_NATIVE_NOSCALE="1")
            subprocess.run(["timeout", "-s", "INT", "55" if cups_only else "80", "./play.sh", "-t"], cwd=game, env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            shots = sorted(glob.glob(os.path.join(game, "user/log/native_dump/shots/shot_*_2560x1440*")))
            pick = [s for s in shots if 3000 <= int(os.path.basename(s).split("_")[1]) <= 3600]
            keep = pick[0] if pick else (shots[-1] if shots else None)
            cupshots = [s for s in shots if 1450 <= int(os.path.basename(s).split("_")[1]) <= 1560]
            if cupshots:
                try:
                    from PIL import Image
                    Image.open(cupshots[0]).resize((960, 540)).save(os.path.join(out, name + "_cup.png"))
                except Exception as e:  # noqa: BLE001
                    print(name, "no cup image:", e)
            if keep:
                try:
                    from PIL import Image
                    Image.open(keep).resize((960, 540)).save(os.path.join(out, name + ".png"))
                except Exception as e:  # noqa: BLE001
                    print(name, "no image:", e)
            crashed = "No recompiled block at PC 0x0" in open(os.path.join(game, "user/log/suyu_log.txt"), errors="ignore").read()
            print(name, "CRASH" if crashed else "ok", "shots", len(shots), "->", os.path.basename(keep) if keep else None, flush=True)
finally:
    shutil.copy(os.path.join(backup, "script0-1.txt"), script)
    for f in ("sdl2-config.ini", "kartrecomp_overlay.ini"):
        shutil.copy(os.path.join(backup, f), os.path.join(cfg, f))
