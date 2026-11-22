#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# test_collect_logs.sh: packaging/linux/collect-logs.sh packs logs, settings and system info, and never keys or game files.
set -uo pipefail
cd "$(dirname "$0")/.."
tmp="$(mktemp -d)"; trap 'rm -rf "$tmp"' EXIT
d="$tmp/share/kartrecomp"; c="$tmp/config/kartrecomp"; k="$tmp/cache/kartrecomp"
mkdir -p "$d/log" "$d/keys" "$d/content" "$d/nand/system" "$d/module" "$c" "$k/build-work"
echo "frame pacing" > "$d/log/suyu_log.txt"; echo "KARTRECOMP_NATIVE_GPU=native" > "$c/launch.env"; echo "[x]" > "$c/qt-config.ini"
echo secret > "$d/keys/prod.keys"; echo k > "$d/keys/keys.txt"; echo nsp > "$d/content/base.nsp"; echo t > "$d/content/notes.txt"
echo p > "$d/nand/system/profile.txt"; echo '{}' > "$d/module/module.json"; echo w > "$k/build-work/build.log"
XDG_DATA_HOME="$tmp/share" XDG_CONFIG_HOME="$tmp/config" XDG_CACHE_HOME="$tmp/cache" bash packaging/linux/collect-logs.sh "$tmp/out.tar.gz" >/dev/null \
  || { echo "FAIL: the script failed"; exit 1; }
list=$(tar -tzf "$tmp/out.tar.gz") || { echo "FAIL: no archive"; exit 1; }
for want in ./data/log/suyu_log.txt ./config/launch.env ./config/qt-config.ini ./system-info.txt; do
  echo "$list" | grep -qxF "$want" || { echo "FAIL: $want missing from: $list"; exit 1; }
done
echo "$list" | grep -E 'keys|content|nand|module|build-work' && { echo "FAIL: private or game files in the archive"; exit 1; }
tar -xzOf "$tmp/out.tar.gz" ./system-info.txt | grep -q '^CPU: ' || { echo "FAIL: no CPU line in system-info.txt"; exit 1; }
XDG_DATA_HOME="$tmp/none" XDG_CONFIG_HOME="$tmp/none" bash packaging/linux/collect-logs.sh "$tmp/x.tar.gz" >/dev/null 2>&1 \
  && { echo "FAIL: must refuse when the game never ran"; exit 1; }
echo PASS
