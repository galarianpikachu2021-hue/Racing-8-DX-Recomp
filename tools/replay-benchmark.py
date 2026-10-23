#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Replays one recorded run in the emulated renderer and the native one and compares frame rates.

    tools/replay-benchmark.py <recording.txt> [--modes emu,native] [--timeout SECONDS] [--env KEY=VAL ...]

Recordings come from the in-game menu (F10, "Record my inputs") or from KARTRECOMP_RECORD=1, which records from the
first frame; they are saved as local/game/user/tas/recordings/run-<time>.txt. A recording made from boot replays the
same menus, races and items every time, so both renderers see the same input. The script copies it to
user/tas/script0-1.txt (the previous one is put back afterwards), runs `play.sh -t` once per mode with per-second
fps logging, and prints the average, median, 1% low and the seconds below 55 fps for each.

Modes: emu = suyu's own renderer, native = the native Vulkan renderer (KARTRECOMP_NATIVE_GPU=native). Extra
environment for the native run (output height, upscaler...) goes in with --env. One game at a time: it refuses to run
while mk8d is already running, and a full run takes as long as the recording (plus loading).
"""
import argparse
import os
import pathlib
import re
import shutil
import statistics
import subprocess
import sys
import time

GAME = pathlib.Path(__file__).resolve().parent.parent / "local" / "game"


def running():
    return subprocess.run(["pgrep", "-x", "mk8d"], capture_output=True).returncode == 0


def run_mode(mode, extra_env, timeout):
    env = dict(os.environ, SUYU_CMD_PERF_SAMPLE="1")
    if mode == "native":
        env["KARTRECOMP_NATIVE_GPU"] = "native"
    env.update(extra_env)
    log = GAME / "user" / "log" / "suyu_log.txt"
    log.unlink(missing_ok=True)
    start = time.time()
    try:
        subprocess.run(["./play.sh", "-t"], cwd=GAME, env=env, timeout=timeout, stdout=subprocess.DEVNULL,
                       stderr=subprocess.DEVNULL)
    except subprocess.TimeoutExpired:
        subprocess.run(["pkill", "-INT", "-x", "mk8d"])
        time.sleep(3)
    wall = time.time() - start
    fps = [float(m.group(1)) for m in re.finditer(rb"game_fps=([0-9.]+)", log.read_bytes())] if log.exists() else []
    saved = GAME / "user" / "log" / f"bench-{mode}.log"
    if log.exists():
        shutil.copy(log, saved)
    return fps, wall


def stats(fps):
    # Per-second samples; the first ones are boot and loading, so start at the first second above 20 fps.
    start = next((i for i, v in enumerate(fps) if v > 20), 0)
    v = sorted(fps[start:])
    if not v:
        return None
    low1 = v[max(0, int(len(v) * 0.01) - 1)]
    return {"seconds": len(v), "avg": statistics.mean(v), "median": statistics.median(v), "1% low": low1,
            "below 55": sum(1 for x in v if x < 55)}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("recording")
    ap.add_argument("--modes", default="emu,native")
    ap.add_argument("--timeout", type=int, default=0, help="per mode; default recording length / 60 + 180 s")
    ap.add_argument("--env", nargs="*", action="extend", default=[], help="extra KEY=VAL for the run")
    a = ap.parse_args()
    rec = pathlib.Path(a.recording)
    if not rec.exists():
        sys.exit(f"{rec}: not found")
    if running():
        sys.exit("mk8d is already running: close it first")
    frames = sum(1 for _ in rec.open())
    timeout = a.timeout or frames // 60 + 180
    script = GAME / "user" / "tas" / "script0-1.txt"
    backup = script.with_suffix(".txt.bench-backup")
    if script.exists():
        shutil.copy(script, backup)
    shutil.copy(rec, script)
    extra = dict(kv.split("=", 1) for kv in a.env)
    results = {}
    try:
        for mode in a.modes.split(","):
            print(f"running {mode} ({frames} frames, timeout {timeout}s)...", flush=True)
            fps, wall = run_mode(mode, extra, timeout)
            results[mode] = stats(fps)
            print(f"  {len(fps)} fps samples in {wall:.0f}s", flush=True)
    finally:
        if backup.exists():
            shutil.move(backup, script)
    print()
    cols = ["seconds", "avg", "median", "1% low", "below 55"]
    print("mode      " + "".join(f"{c:>10}" for c in cols))
    for mode, st in results.items():
        if not st:
            print(f"{mode:<10}no fps samples (did the run reach the game?)")
            continue
        print(f"{mode:<10}" + "".join(f"{st[c]:>10.1f}" if isinstance(st[c], float) else f"{st[c]:>10}" for c in cols))


if __name__ == "__main__":
    main()
