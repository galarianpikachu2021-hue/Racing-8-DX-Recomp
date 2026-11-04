#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# module-vs-static.sh [runs]: A/B of the statically linked game modules against the same modules loaded from shared libraries
# (KARTRECOMP_MODULE_DIR), scripted effects race, mods off, GPU-light. Prints core-0 samples and seconds under 58 fps per run and
# the means. Passes when the module mean is within 3% of the static mean (the noise band) and both runs say "fully static".
# Needs local/spike/modules/recompiled_*.so (build them with tools/spike/build-modules.sh). One game instance at a time.
set -euo pipefail
cd "$(dirname "$0")/../.."
runs="${1:-3}"
moddir="${MODDIR:-$PWD/local/spike/modules}"
[ -e "$moddir/recompiled_image.so" ] || { echo "FAIL: no module build in $moddir"; exit 1; }
pgrep -x mk8d >/dev/null && { echo "FAIL: mk8d is running"; exit 1; }
game="$PWD/local/game"
cfg="$game/user/config"; tas="$game/user/tas"
bk="$(mktemp -d)"
cp -a "$cfg/sdl2-config.ini" "$cfg/kartrecomp_overlay.ini" "$bk/" 2>/dev/null || true
cp -a "$tas/script0-1.txt" "$bk/" 2>/dev/null || true
restore() { cp -a "$bk/sdl2-config.ini" "$bk/kartrecomp_overlay.ini" "$cfg/" 2>/dev/null || true
            cp -a "$bk/script0-1.txt" "$tas/" 2>/dev/null || true; rm -rf "$bk"; }
trap restore EXIT
tools/make-menu-tas.py "$tas/script0-1.txt" --centre-stick --effects >/dev/null
one() {  # <label> [extra env...]
  local label="$1"; shift
  ( cd "$game"
    env KARTRECOMP_NO_MODS=1 SUYU_CMD_PERF_SAMPLE=1 KARTRECOMP_NATIVE_GPU=native KARTRECOMP_NATIVE_NOSCALE=1 KARTRECOMP_NATIVE_SHOTS=900 \
        KARTRECOMP_PROFILE=60,60 KARTRECOMP_PROFILE_OUT="$PWD/user/log/profile-spike-$label.txt" "$@" \
        timeout -s INT 170 "./${KARTRECOMP_PLAY:-play.sh}" -t >/dev/null 2>&1 || true )
  grep -q "fully static" "$game/user/log/recomp_coverage.txt" || { echo "FAIL: $label not fully static"; exit 1; }
  case "$label" in module*) grep -aq "LoadModuleDir: Recompiled module \[3\] loaded from" "$game/user/log/suyu_log.txt" || { echo "FAIL: $label did not load the module libraries"; exit 1; } ;; esac
  local n slow
  n=$(tools/profile-report.py "$game/user/log/profile-spike-$label.txt" "$game/mk8d" --summary | awk '$1=="CPUCore_0"{print $2}')
  slow=$(grep -a 'PERF game_fps' "$game/user/log/suyu_log.txt" | sed -E 's/.*game_fps=([0-9.]+).*/\1/' | awk 'NR>=45&&NR<=135&&$1<58{c++} END{print c+0}')
  echo "$label core0=$n slow_seconds=$slow"
}
sa=0; ma=0
for i in $(seq 1 "$runs"); do
  s=$(one "static$i" | tee /dev/stderr | sed -E 's/.*core0=([0-9]+).*/\1/'); sa=$((sa + s))
  m=$(one "module$i" KARTRECOMP_MODULE_DIR="$moddir" ${KARTRECOMP_USE_ENGINE:+KARTRECOMP_PLAY=play-engine.sh} | tee /dev/stderr | sed -E 's/.*core0=([0-9]+).*/\1/'); ma=$((ma + m))
done
smean=$((sa / runs)); mmean=$((ma / runs))
echo "static mean=$smean module mean=$mmean"
[ $((mmean * 100)) -le $((smean * 103)) ] && echo PASS || { echo "FAIL: module build is more than 3% slower"; exit 1; }
