#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# The Thor-like emulator: default AVD kart_thor (a copy of the user's thor_api33 with a 32 GB data partition; 1920x1080 plus a 1240x1080
# second display), EMU_AVD picks another. emu.sh start [host|swiftshader] | stop | install <apk> | logcat
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
. "$here/env.sh"
case "$1" in
start)
    # The AVD's .ini (shared with the user's other project) names its folder through a space-free link that may be gone: recreate it.
    avd_path=$(sed -n 's/^path=//p' "$ANDROID_AVD_HOME/${EMU_AVD:-kart_thor}.ini")
    if [ -n "$avd_path" ] && [ ! -d "$avd_path" ]; then
        link_root="${avd_path%/toolchain/avd/${EMU_AVD:-kart_thor}.avd}"
        mkdir -p "$(dirname "$link_root")"
        ln -sfn "$(dirname "$(realpath "$ANDROID_AVD_HOME/..")")" "$link_root"
    fi
    gpu="${2:-host}"; [ "$gpu" = swiftshader ] && gpu=swiftshader_indirect
    adb devices | grep -q emulator- && { echo "already running"; exit 0; }
    win=(); [ "${EMU_HEADLESS:-0}" = 1 ] && win=(-no-window)  # visible by default: the user watches it
    setsid nohup emulator -avd ${EMU_AVD:-kart_thor} "${win[@]}" -no-audio -no-snapshot -gpu "$gpu" -memory "${EMU_RAM_MB:-6144}" \
        > "${XDG_CACHE_HOME:-$HOME/.cache}/kartrecomp-emulator.log" 2>&1 &
    adb wait-for-device
    until [ "$(adb shell getprop sys.boot_completed 2>/dev/null | tr -d '\r')" = 1 ]; do sleep 2; done
    echo "emulator up ($gpu)";;
stop) adb emu kill || true;;
install) adb install -r "$2";;
logcat) adb logcat -d;;
*) echo "usage: emu.sh start|stop|install <apk>|logcat"; exit 2;;
esac
