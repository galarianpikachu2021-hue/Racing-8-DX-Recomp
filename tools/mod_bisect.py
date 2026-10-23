#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Finds which installed mod makes a scripted Grand Prix crash ("No recompiled block at PC 0x0"): runs the menu-walk TAS with halves of the mods
switched off (through [DisabledAddOns]) until one mod is left.

    mod_bisect.py <game dir (local/game)> [seconds per run (default 60)]

Run it with the game closed; the config, TAS script and overlay ini are restored at the end.
"""
import os
import re
import shutil
import subprocess
import sys

game = os.path.abspath(sys.argv[1])
seconds = sys.argv[2] if len(sys.argv) > 2 else "60"
tid_dir = os.path.join(game, "mods", "0100152000022000")
cfg = os.path.join(game, "user", "config", "sdl2-config.ini")
script = os.path.join(game, "user", "tas", "script0-1.txt")
ov = os.path.join(game, "user", "config", "kartrecomp_overlay.ini")
tool = os.path.join(os.path.dirname(os.path.abspath(__file__)), "make-menu-tas.py")
bk = {p: open(p, "rb").read() for p in (cfg, script, ov)}
mods = sorted(m for m in os.listdir(tid_dir) if m not in ("!Merged text", "CTGP DX (tracks)") and os.path.isdir(os.path.join(tid_dir, m)))


def set_disabled(names):
    text = open(cfg).read()
    text = re.sub(r"(?m)^3\\disabled\\.*\n", "", text)
    lines = [f"3\\disabled\\size={len(names)}"]
    for i, n in enumerate(names, 1):
        lines += [f"3\\disabled\\{i}\\d\\default=false", f"3\\disabled\\{i}\\d={n}"]
    text = text.replace("3\\title_id=72080821221203968\n", "3\\title_id=72080821221203968\n" + "\n".join(lines) + "\n", 1)
    open(cfg, "w").write(text)


def crashed(enabled):
    set_disabled([m for m in mods if m not in enabled] + ["CTGP DX (tracks)"])
    subprocess.run([sys.executable, tool, script], check=True, stdout=subprocess.DEVNULL)
    lines = [l for l in open(script).read().splitlines() if int(l.split()[0]) <= 3600]
    open(script, "w").write("\n".join(lines + ["30000 NONE 0;0 0;0"]) + "\n")
    log = os.path.join(game, "..", "bisect_run.log")
    with open(log, "w") as lf:
        subprocess.run(["timeout", "-s", "INT", seconds, "./play.sh", "-t"], cwd=game, env=dict(os.environ, KARTRECOMP_NATIVE_GPU="native"), stdout=lf, stderr=subprocess.STDOUT)
    return "No recompiled block" in open(log, errors="replace").read()


try:
    if subprocess.run(["pgrep", "-x", "mk8d"], stdout=subprocess.DEVNULL).returncode == 0:
        sys.exit("a game is already running")
    print("all mods:", "CRASH" if crashed(set(mods)) else "ok", flush=True)
    suspects = list(mods)
    while len(suspects) > 1:
        half = suspects[: len(suspects) // 2]
        bad = crashed(set(half))
        print(f"{len(half)} of {len(suspects)} enabled ({half[0]} ...):", "CRASH" if bad else "ok", flush=True)
        suspects = half if bad else suspects[len(suspects) // 2:]
    print("culprit:", suspects[0])
finally:
    for p, data in bk.items():
        open(p, "wb").write(data)
