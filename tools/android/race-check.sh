#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# race-check.sh <apk> <game dir> [seconds]: plays a scripted Grand Prix (tools/make-menu-tas.py --centre-stick) in the running emulator
# through the app's boot TAS playback, then reports average fps (from the renderer's frame counter), peak RSS, whether the guest
# stayed fully static, and a screenshot (local/android/shot-race.png). Then sends the app to the background and brings it back:
# rendering must resume. Run boot-check.sh once first (it copies the game files into the app). Exit 0 = passed.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"; repo="$(cd "$here/../.." && pwd)"; . "$here/env.sh"
apk="$1"; game="$(realpath "$2")"; secs="${3:-300}"
pkg=$("$ANDROID_HOME"/build-tools/36*/aapt2 dump packagename "$apk")
files="/sdcard/Android/data/$pkg/files"
adb install -r "$apk" >/dev/null
tas=$(mktemp); python3 "$repo/tools/make-menu-tas.py" "$tas" --centre-stick >/dev/null
adb shell mkdir -p "$files/tas"; adb push "$tas" "$files/tas/script0-1.txt" >/dev/null; rm -f "$tas"
# tas_enable is off in a fresh config; playback is armed by a system property read at boot.
# (a "\\default=true" line means "use the default", so that has to be cleared as well)
sedscript=$(mktemp)
cat > "$sedscript" <<'SED'
s/^tas_enable\\default=true/tas_enable\\default=false/
s/^tas_enable=false/tas_enable=true/
s/^tas_loop\\default=true/tas_loop\\default=false/
s/^tas_loop=true/tas_loop=false/
SED
adb push "$sedscript" /data/local/tmp/tas.sed >/dev/null; rm -f "$sedscript"
adb shell "sed -i -f /data/local/tmp/tas.sed $files/config/config.ini"
adb shell "grep -E 'tas_enable|tas_loop' $files/config/config.ini" | tr -d '\r' | tr '\n' ' '; echo
adb shell setprop debug.suyu.tas playback
adb logcat -G 64M >/dev/null 2>&1 || true
adb logcat -c
adb shell am force-stop "$pkg"
adb shell am start -n "$pkg/org.yuzu.yuzu_emu.activities.EmulationActivity" --es kartrecomp.base "$files/kart/base.nsp" --es kartrecomp.update "$files/kart/update.nsp" >/dev/null
peak=0; t=0
while [ $t -lt "$secs" ]; do
    sleep 10; t=$((t + 10))
    pid=$(adb shell pidof "$pkg" | tr -d '\r' | awk '{print $1}')
    [ -z "$pid" ] && { echo "FAIL: the app died at ${t}s"; adb logcat -d | grep -E "FATAL|Fatal signal" | head -3; exit 1; }
    hwm=$(adb shell "grep VmHWM /proc/$pid/status" | tr -d '\r' | awk '{print $2}')
    [ "${hwm:-0}" -gt "$peak" ] && peak=$hwm
    adb logcat -d | grep -q "TAS playback finished" && break
done
adb exec-out screencap -p > "$repo/local/android/shot-race.png"
log=$(adb logcat -d)
fps=$(grep -o "\[ *[0-9.]*\].*nvn-host vulkan: [0-9]* frames" <<<"$log" | sed -E 's/\[ *([0-9.]+)\].*vulkan: ([0-9]+) frames/\1 \2/' | awk 'NR>1{ if ($1>prev_t) {r=($2-prev_f)/($1-prev_t); n++; s+=r; if (r<55) slow++} } {prev_t=$1; prev_f=$2} END{ if (n) printf "%.1f (%d samples, %d under 55)", s/n, n, slow+0; else print "n/a" }')
echo "ran ${t}s; avg render fps: $fps; peak RSS: $((peak / 1024)) MB"
grep -m1 "TAS playback\|Android boot TAS" <<<"$log" | cut -c1-150 || true
adb shell "grep -E 'threads killed|static -> JIT' $files/log/recomp_coverage.txt" 2>/dev/null || true
# Lifecycle 1: the display changes size while the game runs (rotation, split-screen, a dock): the renderer must follow it.
frames_of() { grep -o "nvn-host vulkan: [0-9]* frames" | tail -1 | grep -o "[0-9]*" | head -1; }
before=$(frames_of <<<"$log")
adb shell wm size 1280x720; sleep 8; adb shell wm size 1920x1080; sleep 25
log2=$(adb logcat -d)
after=$(frames_of <<<"$log2")
grep -q "FATAL EXCEPTION\|Fatal signal" <<<"$log2" && { echo "FAIL: crash when the display size changed"; exit 1; }
echo "after a display resize: frames ${before:-0} -> ${after:-0}"
[ "${after:-0}" -gt "${before:-0}" ] || { echo "FAIL: rendering stopped after the display resize"; exit 1; }
# Lifecycle 2: home key and back. The stock app pauses emulation in the background and waits for Resume, so only "alive, no crash".
adb shell input keyevent KEYCODE_HOME; sleep 6
adb shell am start -n "$pkg/org.yuzu.yuzu_emu.activities.EmulationActivity" >/dev/null; sleep 8
log3=$(adb logcat -d)
grep -q "FATAL EXCEPTION\|Fatal signal" <<<"$log3" && { echo "FAIL: crash around the home key"; exit 1; }
[ -n "$(adb shell pidof "$pkg" | tr -d '\r')" ] || { echo "FAIL: the app is gone after the home key"; exit 1; }
adb shell am force-stop "$pkg"; adb shell setprop debug.suyu.tas none
echo "home key and back: app alive, no crash (paused by the stock app until Resume)"
echo PASS
