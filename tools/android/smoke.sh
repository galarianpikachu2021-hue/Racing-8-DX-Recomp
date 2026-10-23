#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# smoke.sh <apk>: installs the APK in the running emulator, starts the main activity and passes when it is displayed with no crash.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"; . "$here/env.sh"
adb install -r "$1" >/dev/null
pkg=$(aapt2 dump packagename "$1" 2>/dev/null || "$ANDROID_HOME"/build-tools/36*/aapt2 dump packagename "$1")
adb logcat -c
adb shell am start -W -n "$pkg/org.yuzu.yuzu_emu.ui.main.MainActivity" >/dev/null
sleep 6
log=$(adb logcat -d)
if grep -q "FATAL EXCEPTION" <<<"$log"; then grep -A8 "FATAL EXCEPTION" <<<"$log" | head -12; echo "FAIL: crash"; exit 1; fi
grep -q "Displayed $pkg/" <<<"$log" || { echo "FAIL: main activity not displayed"; exit 1; }
echo "PASS: $pkg main activity displayed, no crash"
