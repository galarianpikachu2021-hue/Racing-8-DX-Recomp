#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Builds the fork's Android app: build-app.sh <flavor: chromeOS (x86_64) | mainline (arm64)> <buildtype: relWithDebInfo|release|debug> [--recomp <exefs tree>]
# The APK is copied to <repo>/local/android/. RAM-capped (KARTRECOMP_BUILD_MEM, default 8G) and niced: the desktop is shared.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"; repo="$(cd "$here/../.." && pwd)"
. "$here/env.sh"
flavor="$1"; type="$2"; shift 2
fork="${KARTRECOMP_FORK:-$HOME/reverse-engineering/mk8d/mk8-recomp/third_party/suyu}"
props=()
if [ "${1:-}" = "--recomp" ]; then props+=("-Pkartrecomp.recompDir=$(realpath "$2")"); fi
task="assemble${flavor^}${type^}"
out="$repo/local/android"; mkdir -p "$out"
cd "$fork/src/android"
systemd-run --user --scope -q -p MemoryMax="${KARTRECOMP_BUILD_MEM:-8G}" nice -n 19 ./gradlew --no-daemon -Dorg.gradle.workers.max=4 "${props[@]}" "$task"
apk=$(find app/build/outputs/apk -name "*${flavor}*${type}*.apk" -newer "$here/build-app.sh" -print -quit 2>/dev/null || true)
[ -z "$apk" ] && apk=$(ls -t $(find app/build/outputs/apk -name "*.apk") | head -1)
suffix=""; [ ${#props[@]} -gt 0 ] && suffix="-recomp"
cp "$apk" "$out/app-$flavor-$type$suffix.apk"
ls -l "$out/app-$flavor-$type$suffix.apk"
