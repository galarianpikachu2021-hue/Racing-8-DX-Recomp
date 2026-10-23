#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Scores the motion vectors on a scripted race: runs the game with KARTRECOMP_MV_DEBUG=4 (the picture becomes the reprojection error: last
frame warped by the vectors against this frame, x8, grey = good) and reads the shots. Lower is better.

    tools/mv-eval.py <game dir> [label]      (game closed; restores the TAS script)
"""
import glob, os, shutil, subprocess, sys
import numpy as np
from PIL import Image

game = sys.argv[1]
label = sys.argv[2] if len(sys.argv) > 2 else "run"
here = os.path.dirname(os.path.abspath(__file__))
script = os.path.join(game, "user", "tas", "script0-1.txt")
backup = script + ".mvbak"
shutil.copy(script, backup)
try:
    subprocess.run([sys.executable, os.path.join(here, "make-menu-tas.py"), script, "--cup=1", "--effects"], check=True, stdout=subprocess.DEVNULL)
    shutil.rmtree(os.path.join(game, "user/log/native_dump/shots"), ignore_errors=True)
    env = dict(os.environ, KARTRECOMP_NATIVE_GPU="native", KARTRECOMP_NATIVE_HEIGHT="1440", KARTRECOMP_UPSCALER="fsr3", KARTRECOMP_MV_DEBUG="4",
               KARTRECOMP_NATIVE_SHOTS="90")
    subprocess.run(["timeout", "-s", "INT", "115", "./play.sh", "-t"], cwd=game, env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
finally:
    shutil.move(backup, script)
shots = sorted(glob.glob(os.path.join(game, "user/log/native_dump/shots/shot_*_2560x1440*")))
vals = []
for s in shots:
    f = int(os.path.basename(s).split("_")[1])
    if f < 3300 or f > 5400:
        continue
    im = np.asarray(Image.open(s).convert("RGB"), dtype=np.float32)[:1100, :, 0] / 255.0  # the 3D picture, not the HUD strip
    vals.append((f, float(im.mean()), float((im > 0.5).mean())))
if not vals:
    print("no shots")
    sys.exit(1)
m = np.mean([v[1] for v in vals]); bad = np.mean([v[2] for v in vals])
print(f"{label}: frames {len(vals)}  mean error {m:.4f}  pixels with error > 0.5: {100 * bad:.2f}%")
for f, a, b in vals:
    print(f"   {f}: {a:.4f} {100 * b:.2f}%")
