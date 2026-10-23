#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# boot-check.sh <apk> <game dir> [wrong-update.nsp]: boots the recompiled game in the running emulator from an explicit base +
# update and passes when the guest runs fully static. Copies (once) content/base.nsp, content/update-3.0.4.nsp, the keys and the
# firmware from the local game folder into the app's own storage (never the repo). With a third argument it boots that NSP as the
# update instead and passes when the app refuses it.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"; . "$here/env.sh"
apk="$1"; game="$(realpath "$2")"; wrong="${3:-}"
pkg=$("$ANDROID_HOME"/build-tools/36*/aapt2 dump packagename "$apk")
adb install -r "$apk" >/dev/null
adb shell am start -W -n "$pkg/org.yuzu.yuzu_emu.ui.main.MainActivity" >/dev/null; sleep 4; adb shell am force-stop "$pkg"  # creates its folders
files="/sdcard/Android/data/$pkg/files"
push_once() { adb shell "[ -s '$2' ]" 2>/dev/null || { adb shell mkdir -p "$(dirname "$2")"; adb push "$1" "$2" >/dev/null; }; }
push_once "$game/content/base.nsp" "$files/kart/base.nsp"
push_once "$game/content/update-3.0.4.nsp" "$files/kart/update.nsp"
home="${KARTRECOMP_SUYU_HOME:-$game/../suyu-home/suyu}"
for k in prod.keys title.keys; do push_once "$home/keys/$k" "$files/keys/$k"; done
adb shell "[ -d '$files/nand/system/Contents/registered' ]" || { adb shell mkdir -p "$files/nand/system/Contents"; adb push "$home/nand/system/Contents/registered" "$files/nand/system/Contents/" >/dev/null; }
update="$files/kart/update.nsp"
if [ -n "$wrong" ]; then push_once "$wrong" "$files/kart/wrong.nsp"; update="$files/kart/wrong.nsp"; fi
adb logcat -c
adb shell am start -n "$pkg/org.yuzu.yuzu_emu.activities.EmulationActivity" --es kartrecomp.base "$files/kart/base.nsp" --es kartrecomp.update "$update" >/dev/null
sleep "${BOOT_WAIT:-45}"
log=$(adb logcat -d)
adb shell am force-stop "$pkg"
if [ -n "$wrong" ]; then
    grep -q "kart-recomp: explicit update refused" <<<"$log" && { echo "PASS: wrong update refused"; exit 0; }
    echo "FAIL: wrong update was not refused"; exit 1
fi
grep -q "FATAL EXCEPTION\|Fatal signal" <<<"$log" && { grep -m3 -A3 "FATAL EXCEPTION\|Fatal signal" <<<"$log"; echo "FAIL: crash"; exit 1; }
mods=$(grep -c "Static recompiled module" <<<"$log" || true)
run=$(grep -c "ArmRecomp::RunThread entered" <<<"$log" || true)
echo "static modules: $mods, guest threads entered: $run"
[ "$mods" -ge 4 ] && [ "$run" -ge 1 ] && echo PASS || { echo "FAIL: the recompiled game did not start"; exit 1; }
