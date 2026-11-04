#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# build-modules.sh <export tree> <out dir>: builds each of the export's module projects separately (their target names collide in one
# project) as shared images recompiled_<name>.so, loaded by the engine through KARTRECOMP_MODULE_DIR. clang, hot code -O2 and cold
# code -O0, x86-64-v3. Heavy: run it under systemd-run with a memory cap.
set -euo pipefail
tree="$(realpath "$1")"; out="$(realpath -m "$2")"
work="${XDG_CACHE_HOME:-$HOME/.cache}/kartrecomp-tools/work-modules"
mkdir -p "$out" "$work"
for pair in rtld:rtld main:image subsdk0:subsdk0 sdk:sdk; do
  dir=${pair%%:*}; name=${pair##*:}
  [ -f "$tree/$dir/CMakeLists.txt" ] || continue
  target=recompiled_$name
  cmake -S "$tree/$dir" -B "$work/$dir" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang \
    -DRECOMP_BUILD_STATIC_LIB=OFF -DRECOMP_COLD_FLAGS=-O0 -DRECOMP_JOBS="${RECOMP_JOBS:-4}" \
    "-DRECOMP_OPT_FLAGS_V2=-O2 -foptimize-sibling-calls -ffunction-sections -march=x86-64-v3" >"$work/$dir.log" 2>&1 \
    || { tail -20 "$work/$dir.log"; exit 1; }
  cmake --build "$work/$dir" --target "$target" >>"$work/$dir.log" 2>&1 || { tail -20 "$work/$dir.log"; exit 1; }
  cp "$work/$dir/lib$target.so" "$out/$target.so"
  echo "built $target"
done
ls -l "$out"
