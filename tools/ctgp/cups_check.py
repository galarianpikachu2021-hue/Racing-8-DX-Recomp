#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Loads each extra CTGP track as race 1 of a scripted Grand Prix and keeps a screenshot, to check that it loads.

    cups_check.py <game dir (local/game)> <mods dir> <out dir> [CT_xx ...]      (no names = every track in the mods dir)

Run it with the game closed. Sets KARTRECOMP_EXTRA_CUP for each run (the overlay deletes a stale extra_cup.txt at start-up); backs up and restores the TAS script
and configs. Prints CRASH (log shows a guest crash) or ok per track.
"""
import glob
import os
import shutil
import subprocess
import sys

game, mods, out = [os.path.abspath(a) for a in sys.argv[1:4]]
names = sys.argv[4:]
tid = "0100152000022000"
if not names:
    names = sorted({os.path.basename(p) for p in glob.glob(os.path.join(mods, tid, "*", "romfs", "Course", "CT_*"))})
here = os.path.dirname(os.path.abspath(__file__))
tool = os.path.join(here, "..", "make-menu-tas.py")
script = os.path.join(game, "user", "tas", "script0-1.txt")
cfg = os.path.join(game, "user", "config")
os.makedirs(out, exist_ok=True)
backup = os.path.join(out, "_backup")
os.makedirs(backup, exist_ok=True)
shutil.copy(script, os.path.join(backup, "script0-1.txt"))
for f in ("sdl2-config.ini", "kartrecomp_overlay.ini"):
    shutil.copy(os.path.join(cfg, f), os.path.join(backup, f))
if subprocess.run(["pgrep", "-x", "mk8d"], stdout=subprocess.DEVNULL).returncode == 0:
    sys.exit("a game is already running")
try:
    subprocess.run([sys.executable, tool, script], check=True, stdout=subprocess.DEVNULL)
    lines = [l for l in open(script).read().splitlines() if int(l.split()[0]) <= 4300]
    open(script, "w").write("\n".join(lines + ["30000 NONE 0;0 0;0"]) + "\n")
    for name in names:
        shutil.rmtree(os.path.join(game, "user/log/native_dump/shots"), ignore_errors=True)
        env = dict(os.environ, KARTRECOMP_EXTRA_CUP=f"{name} {name} {name} {name}", KARTRECOMP_MODS_DIR=mods, KARTRECOMP_NATIVE_SHOTS="150", KARTRECOMP_NATIVE_GPU="native", KARTRECOMP_NATIVE_NOSCALE="1")
        log = os.path.join(out, name + ".log")
        with open(log, "w") as lf:
            subprocess.run(["timeout", "-s", "INT", "85", "./play.sh", "-t"], cwd=game, env=env, stdout=lf, stderr=subprocess.STDOUT)
        text = open(log, errors="replace").read()
        crash = "No recompiled block" in text or "Unmapped" in text
        shots = sorted(glob.glob(os.path.join(game, "user/log/native_dump/shots/shot_*_2560x1440*")))
        pick = [s for s in shots if 3900 <= int(os.path.basename(s).split("_")[1]) <= 4500]
        keep = pick[0] if pick else (shots[-1] if shots else None)
        if keep:
            try:
                from PIL import Image
                Image.open(keep).resize((960, 540)).save(os.path.join(out, name + ".png"))
            except Exception as e:  # noqa: BLE001
                print(name, "no image:", e)
        print(name, "CRASH" if crash else "ok", flush=True)
finally:
    for f in ("sdl2-config.ini", "kartrecomp_overlay.ini"):
        shutil.copy(os.path.join(backup, f), os.path.join(cfg, f))
    shutil.copy(os.path.join(backup, "script0-1.txt"), script)
    for p in (os.path.join(game, "extra_cup.txt"),):
        if os.path.exists(p):
            os.remove(p)
    shutil.rmtree(os.path.join(game, "user/log/native_dump/shots"), ignore_errors=True)
