#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# acceptance-linux.sh <release AppImage> [seconds of play]: the clean-machine run. A fresh HOME and XDG folders, NO compiler on PATH, only
# the AppImage and the user's own files: setup (validate, install, export, compile, verify), then the built game is started for real and
# must run fully static at menu speed. Also checks nothing outside the fresh folders was touched. Heavy: run under systemd-run.
# Files: KARTRECOMP_BASE / _UPDATE / _KEYS / _FIRMWARE (defaults: this repo's local/ copies).
set -uo pipefail
cd "$(dirname "$0")/.."
img="$(realpath "${1:-}" 2>/dev/null)" || { echo "usage: $0 <AppImage>"; exit 1; }
[ -x "$img" ] || { echo "FAIL: no AppImage at ${1:-}"; exit 1; }
play="${2:-60}"
base="${KARTRECOMP_BASE:-$PWD/local/game/content/base.nsp}"; upd="${KARTRECOMP_UPDATE:-$PWD/local/game/content/update-3.0.4.nsp}"
keys="${KARTRECOMP_KEYS:-$PWD/local/suyu-home/suyu/keys/prod.keys}"
fw="${KARTRECOMP_FIRMWARE:-$PWD/local/suyu-home/suyu/nand/system/Contents/registered}"
mkdir -p "$PWD/local/spike"; fresh="$(mktemp -d -p "$PWD/local/spike")"  # on real disk: /tmp is RAM; trap 'rm -rf "$fresh"' EXIT
marker="$fresh/marker"; touch "$marker"
cd "$fresh" && "$img" --appimage-extract >/dev/null 2>&1 && cd - >/dev/null || { echo "FAIL: cannot unpack the AppImage"; exit 1; }
# PATH holds basic tools only (no compiler of any kind): the build must use the compiler that ships in the AppImage.
mkdir -p "$fresh/path"
for t in bash sh env dirname readlink grep cat mkdir rm ln cp mv sed awk date uname ls sleep timeout nice basename tr head tail sort; do
  p="$(command -v "$t")" && ln -sf "$p" "$fresh/path/$t"
done
run() { env -i HOME="$fresh/home" PATH="$fresh/path" XDG_DATA_HOME="$fresh/home/.local/share" XDG_CONFIG_HOME="$fresh/home/.config" XDG_CACHE_HOME="$fresh/home/.cache" \
          DISPLAY="${DISPLAY:-}" WAYLAND_DISPLAY="${WAYLAND_DISPLAY:-}" XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-}" "$@"; }
mkdir -p "$fresh/home"
echo "[1/3] setup from the user's files (no compiler on PATH)"
start=$(date +%s)
run "$fresh/squashfs-root/AppRun" setup --base "$base" --update "$upd" --keys "$keys" --firmware "$fw" > "$fresh/setup.out" 2> "$fresh/setup.err"
rc=$?
echo "    exit $rc after $(( $(date +%s) - start )) s"
[ $rc -eq 0 ] || { echo "FAIL: setup: $(tail -3 "$fresh/setup.err")"; exit 1; }
data="$fresh/home/.local/share/kartrecomp"
[ -f "$data/module/module.json" ] && [ -f "$data/module/recompiled_image.so" ] || { echo "FAIL: no verified module after setup"; exit 1; }
echo "[2/3] playing for $play s"
run SUYU_CMD_PERF_SAMPLE=1 KARTRECOMP_NATIVE_GPU=native KARTRECOMP_NATIVE_NOSCALE=1 KARTRECOMP_APP_NAME=kartrecomp \
  timeout -s INT "$play" "$fresh/squashfs-root/AppRun" engine --module-dir "$data/module" --content-base "$data/content/base.nsp" \
  --content-update "$data/content/update-3.0.4.nsp" >/dev/null 2>&1
log="$data/log/suyu_log.txt"
grep -aq "fully static" "$data/log/recomp_coverage.txt" || { echo "FAIL: the played game was not fully static"; exit 1; }
fps=$(grep -a 'PERF game_fps' "$log" | sed -E 's/.*game_fps=([0-9.]+).*/\1/' | tail -20 | awk '{s+=$1; n++} END {if (n) printf "%.1f", s/n; else print 0}')
echo "    average game fps over the last 20 s: $fps"
awk -v f="$fps" 'BEGIN {exit !(f >= 55)}' || { echo "FAIL: the game runs at $fps fps in the menus (wanted 55 or more)"; exit 1; }
echo "[3/3] nothing outside the fresh folders was touched"
touched=$(find "$HOME/.local/share/kartrecomp" "$HOME/.config/kartrecomp" "$HOME/.cache/kartrecomp" -newer "$marker" 2>/dev/null | head -3)
[ -z "$touched" ] || { echo "FAIL: the run changed your real folders: $touched"; exit 1; }
echo PASS
