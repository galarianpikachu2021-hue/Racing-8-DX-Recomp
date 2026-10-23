#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Exits 0 when everything the fork's Android app needs is installed; otherwise lists what is missing.
here="$(cd "$(dirname "$0")" && pwd)"
. "$here/env.sh"
missing=()
[ -d "$ANDROID_HOME/platforms/android-36" ] || missing+=("platforms;android-36")
[ -d "$ANDROID_NDK_HOME" ] || missing+=("ndk;$ANDROID_NDK_VERSION")
ls -d "$ANDROID_HOME"/build-tools/36* >/dev/null 2>&1 || missing+=("build-tools;36.0.0")
[ -x "$ANDROID_HOME/cmake/3.31.6/bin/cmake" ] || missing+=("cmake;3.31.6")
[ -d "$ANDROID_AVD_HOME/thor_api33.avd" ] || missing+=("AVD thor_api33")
command -v java >/dev/null || missing+=("JDK 17 ($JAVA_HOME)")
if [ ${#missing[@]} -gt 0 ]; then printf 'missing: %s\n' "${missing[@]}"; exit 1; fi
echo "android toolchain ok (NDK $ANDROID_NDK_VERSION)"
