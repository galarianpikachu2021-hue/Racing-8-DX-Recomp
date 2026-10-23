#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# check-recomp-lib.sh <apk> [unstripped .so]: passes when the APK's native library has the statically recompiled game linked in.
# The APK's copy is stripped, so the symbols are read from the build's unstripped library (default: the newest under the fork's
# app/build/intermediates/merged_native_libs for the APK's ABI); the APK's copy must be at least 400 MB (the game's code).
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"; . "$here/env.sh"
tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
unzip -q -o "$1" 'lib/*/libyuzu-android.so' -d "$tmp"
apk_so=$(find "$tmp" -name libyuzu-android.so | head -1)
abi=$(basename "$(dirname "$apk_so")")
fork="${KARTRECOMP_FORK:-$HOME/reverse-engineering/mk8d/mk8-recomp/third_party/suyu}"
so="${2:-$(ls -t "$fork"/src/android/app/build/intermediates/merged_native_libs/*/*/out/lib/"$abi"/libyuzu-android.so 2>/dev/null | head -1)}"
[ "$(stat -c %s "$apk_so")" -ge 400000000 ] || { echo "FAIL: APK library is only $(stat -c %s "$apk_so") bytes"; exit 1; }
nm=$(ls "$ANDROID_NDK_HOME"/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-nm)
reg=$("$nm" --defined-only "$so" 2>/dev/null | grep -c " suyu_recomp_static_modules_v4$" || true)
blocks=$("$nm" --defined-only "$so" 2>/dev/null | grep -c " blk_main_" || true)
echo "$abi: apk lib $(stat -c %s "$apk_so") B; unstripped: registry=$reg main_blocks=$blocks size=$(stat -c %s "$so")"
[ "$reg" -ge 1 ] && [ "$blocks" -gt 100000 ] && echo PASS || { echo "FAIL: no statically recompiled game in the library"; exit 1; }
