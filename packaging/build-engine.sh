#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# build-engine.sh <fork source dir> <build dir> <out dir>: builds mk8d-engine (the fork's runtime with no game code, strict no-JIT) and
# collects it with the plugins into <out dir>/engine. ENGINE_VERSION = hash of our patches + ABI, the stamp the builder writes into
# the game modules' module.json and the engine checks before loading them. Heavy the first time (hours): run it under systemd-run
# with a memory cap.
set -euo pipefail
fork="$(realpath "$1")"; build="$(realpath -m "$2")"; out="$(realpath -m "$3")"
here="$(cd "$(dirname "$0")/.." && pwd)"
# KARTRECOMP_ENGINE_VERSION overrides the stamp: a release that changes no generated code or module ABI keeps the previous release's
# stamp, so players' built games stay valid (v0.2.2 keeps v0.2.1's d201f1ec0e-abi6).
version="${KARTRECOMP_ENGINE_VERSION:-$(cat "$here"/patches/suyu/*.patch | sha256sum | cut -c1-10)-abi6}"
compilers=()
[ -f "$build/CMakeCache.txt" ] || compilers=(-DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++)  # an existing tree keeps its compiler
cmake -S "$fork" -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DSUYU_NO_JIT=ON -DKARTRECOMP_ENGINE=ON \
  -DKARTRECOMP_ENGINE_VERSION="$version" "${compilers[@]}" >"$build.configure.log" 2>&1 \
  || { tail -20 "$build.configure.log"; exit 1; }
ninja -C "$build" -j"${JOBS:-3}" mk8d-engine
mkdir -p "$out/engine/plugins"
cp "$build/bin/mk8d-engine" "$out/engine/"
strip -s "$out/engine/mk8d-engine" 2>/dev/null || true
# FSR 3 upscaling and frame generation (MIT, built by plugins/fsr3/build.sh) ship; the DLSS plugin never does (NVIDIA's licence).
fsr=""
for d in "${KARTRECOMP_PLUGINS_DIR:-}" "$HOME/.cache/kartrecomp-fsr3" "$here/local/game/plugins"; do
  [ -n "$d" ] && [ -f "$d/libkr_upscaler_fsr3.so" ] && { fsr="$d/libkr_upscaler_fsr3.so"; break; }
done
if [ -n "$fsr" ]; then
  cp "$fsr" "$out/engine/plugins/"
  ln -sf libkr_upscaler_fsr3.so "$out/engine/plugins/libkr_framegen_fsr3.so"
else
  echo "WARNING: libkr_upscaler_fsr3.so not found: the release would have no FSR 3 plugins (run plugins/fsr3/build.sh)" >&2
fi
echo "$version" > "$out/engine/ENGINE_VERSION"
ls -l "$out/engine"
