#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Runs the game (no mods) under a scripted menu walk with block-coverage logging and keeps the first-hit log.
#   coverage_run.sh <game dir> <out file> <last TAS frame> [extra env VAR=value ...]
# Needs a build with native block hooks (tools/recomp/add_block_hooks.py). Backs up and restores the TAS script and configs.
# The log has one line per block of `main`, the first time it ran: "ms offset lr x0 x1 x2 x3". Run it once with the TAS cut at the
# cup screen (frame 1300) and once past a menu action, then diff the offsets to see which code the action ran.
set -euo pipefail
game="$(realpath "$1")"; out="$(realpath -m "$2")"; last="$3"; shift 3
here="$(cd "$(dirname "$0")" && pwd)"
bk="$(mktemp -d)"
pgrep -x mk8d >/dev/null && { echo "a game is already running" >&2; exit 1; }
cp "$game/user/tas/script0-1.txt" "$game/user/config/sdl2-config.ini" "$game/user/config/kartrecomp_overlay.ini" "$bk/"
restore() { cp "$bk/script0-1.txt" "$game/user/tas/"; cp "$bk/sdl2-config.ini" "$bk/kartrecomp_overlay.ini" "$game/user/config/"; rmdir "$game/mods-none" 2>/dev/null || true; }
trap restore EXIT
python3 "$here/../make-menu-tas.py" "$game/user/tas/script0-1.txt" >/dev/null
awk -v n="$last" '$1<=n' "$game/user/tas/script0-1.txt" > "$bk/cut.txt"
for f in 30000 60000; do echo "$f NONE 0;0 0;0" >> "$bk/cut.txt"; done
cp "$bk/cut.txt" "$game/user/tas/script0-1.txt"
rm -f "$game/user/log/block_first_hits.txt"
( cd "$game" && env KARTRECOMP_BLOCK_COVERAGE=1 KARTRECOMP_NO_MODS=1 KARTRECOMP_NATIVE_GPU=native "$@" ./play.sh -t >/dev/null 2>&1 & )
# run until no new blocks appear for 25 s (and at least 60 s)
sleep 60; prev=-1
while pgrep -x mk8d >/dev/null; do
  n=$(wc -l < "$game/user/log/block_first_hits.txt" 2>/dev/null || echo 0)
  [ "$n" = "$prev" ] && break
  prev=$n; sleep 25
done
pkill -x mk8d || true; sleep 3
cp "$game/user/log/block_first_hits.txt" "$out"
echo "kept $(wc -l < "$out") blocks in $out"
