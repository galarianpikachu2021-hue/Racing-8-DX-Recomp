#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# collect-logs.sh [out.tar.gz]: the Linux Collect-Logs.cmd. Packs the launcher's and the game's logs and settings, plus a
# system-info.txt, into ~/kartrecomp-logs.tar.gz (never the game files, keys, firmware, saves or the built game).
# From the release AppImage: ./Racing_8_DX_Recomp-x86_64.AppImage logs
set -uo pipefail
name=kartrecomp
data="${XDG_DATA_HOME:-$HOME/.local/share}/$name"
conf="${XDG_CONFIG_HOME:-$HOME/.config}/$name"
cache="${XDG_CACHE_HOME:-$HOME/.cache}/$name"
# A folder chosen on the launcher's Storage tab (an SD card).
store="$(head -n1 "$conf/storage.txt" 2>/dev/null)"
[ -n "$store" ] && [ -d "$store" ] && { data="$store/data/$name"; cache="$store/cache/$name"; }
out="$(realpath -m "${1:-$HOME/kartrecomp-logs.tar.gz}")"
[ -d "$data" ] || [ -d "$conf" ] || { echo "No $data or $conf yet: run the game first." >&2; exit 1; }
tmp="$(mktemp -d)"; trap 'rm -rf "$tmp"' EXIT

{
  echo "== system info $(date -Iseconds)"
  echo "OS: $(. /etc/os-release 2>/dev/null; echo "${PRETTY_NAME:-unknown}") | kernel $(uname -r)"
  echo "CPU: $(grep -m1 'model name' /proc/cpuinfo | cut -d: -f2- | sed 's/^ *//') | $(nproc) threads"
  echo "RAM: $(awk '/MemTotal/ {t=$2} /MemAvailable/ {a=$2} END {printf "%.1f GB total, %.1f GB free", t/1048576, a/1048576}' /proc/meminfo)"
  echo "CPU governor: $(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor 2>/dev/null || echo unknown)"
  command -v powerprofilesctl >/dev/null && echo "Power profile: $(powerprofilesctl get 2>/dev/null)"
  command -v lspci >/dev/null && lspci 2>/dev/null | grep -Ei 'vga|3d|display' | sed 's/^/GPU: /'
  command -v nvidia-smi >/dev/null && echo "NVIDIA driver: $(nvidia-smi --query-gpu=driver_version --format=csv,noheader 2>/dev/null)"
  command -v vulkaninfo >/dev/null && vulkaninfo --summary 2>/dev/null | grep -E 'deviceName|driverName|driverInfo|apiVersion' | sed 's/^ */Vulkan: /'
  echo "Session: ${XDG_SESSION_TYPE:-unknown}, desktop ${XDG_CURRENT_DESKTOP:-unknown}, gamescope $(pgrep -x gamescope >/dev/null && echo running || echo no)"
  command -v xrandr >/dev/null && xrandr --current 2>/dev/null | grep '\*' | sed 's/^ */Display: /'
} > "$tmp/system-info.txt"

# Same file types as Collect-Logs.cmd; content, keys, nand (firmware, saves, profile), module (built game) and the build caches stay out.
for pair in "data=$data" "config=$conf" "cache=$cache"; do
  dest="$tmp/${pair%%=*}"; root="${pair#*=}"
  [ -d "$root" ] || continue
  (cd "$root" && find . \( -path ./content -o -path ./keys -o -path ./nand -o -path ./module -o -path ./export -o -path ./build-work \) -prune \
     -o -type f \( -name '*.log' -o -name '*.txt' -o -name '*.ini' -o -name '*.json' -o -name '*.env' \) -print0 \
   | while IFS= read -r -d '' f; do mkdir -p "$dest/$(dirname "$f")"; cp "$f" "$dest/$f"; done)
done

tar -czf "$out" -C "$tmp" . || { echo "Could not write $out" >&2; exit 1; }
echo "Wrote $out: attach it to your GitHub issue."
command -v notify-send >/dev/null && notify-send "Racing 8 DX Recomp" "Logs written to $out"
exit 0
