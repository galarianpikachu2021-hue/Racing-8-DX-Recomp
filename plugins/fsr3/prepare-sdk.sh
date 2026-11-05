#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Fetches AMD's FidelityFX SDK v1.1.4 (MIT) into a work folder, applies our local edits, builds AMD's shader compiler (ported to Linux)
# and generates the FSR 3.1 / frame interpolation / optical flow SPIR-V shader blobs. The blobs are plain data, the same for every
# platform: the Windows plugin build takes them from here. Nothing third-party is committed.
#   plugins/fsr3/prepare-sdk.sh [work dir]     (default: ~/.cache/kartrecomp-fsr3)
# Output: <work>/sdk (patched), <work>/shaders
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
work="$(realpath -m "${1:-$HOME/.cache/kartrecomp-fsr3}")"
mkdir -p "$work"
export FSR3_WORK="$work"

# 1. The SDK (v1.1.4), plus local edits for a Linux build: 4-byte wchar_t makes the context structs bigger, and an alignas(32) the
#    backend places at unaligned addresses (the code assumes MSVC's unaligned vector moves) is dropped.
if [ ! -d "$work/sdk/sdk" ]; then
  git clone --depth 1 --branch v1.1.4 https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK.git "$work/sdk"
  sed -i 's/#define FFX_SDK_DEFAULT_CONTEXT_SIZE (1024 \* 128)/#define FFX_SDK_DEFAULT_CONTEXT_SIZE (1024 * 320)/' "$work/sdk/sdk/include/FidelityFX/host/ffx_types.h"
  sed -i 's/typedef struct alignas(32) EffectContext/typedef struct EffectContext/' "$work/sdk/sdk/src/backends/vk/ffx_vk.cpp"
  # On AMD (wave 32/64) FSR 3.1 picks a Lanczos look-up table for its history filter, but this Vulkan backend never uploads the table's
  # data (it stays empty and smears the picture); the shader's own approximation is used instead. With it: sharpness and detail match
  # AMD's FSR 3.1.5 (39.6 dB on dumped frames; with the empty table 32 dB).
  sed -i 's/useLut         = true;/useLut         = false;/' "$work/sdk/sdk/src/components/fsr3upscaler/ffx_fsr3upscaler.cpp"
fi
sdk="$work/sdk/sdk"

# 2. AMD's shader compiler tool, ported to Linux (GLSL path only), and the FSR 3 shader blobs.
#    (Fortify is off: the ported tool has benign over-reads that Ubuntu's default _FORTIFY_SOURCE=3 turns into aborts.)
if [ ! -x "$work/ffxsc/ffx_sc" ]; then
  python3 "$here/port-ffx-sc.py" "$sdk/tools/ffx_shader_compiler" "$work/ffxsc"
  ( cd "$work/ffxsc" && mkdir -p obj
    SCINC=(-U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0 -Isrc -Ilibs/SPIRV-Reflect -Ilibs/SPIRV-Reflect/include -Ilibs/tiny-process-library -Ilibs/MD5)
    gcc -c -O1 "${SCINC[@]}" libs/SPIRV-Reflect/spirv_reflect.c -o obj/spirv_reflect.o
    for f in src/ffx_sc.cpp src/glsl_compiler.cpp src/utils.cpp libs/MD5/md5.cpp libs/tiny-process-library/process.cpp libs/tiny-process-library/process_unix.cpp; do
      g++ -std=c++17 -O1 -w -c "${SCINC[@]}" "$f" -o "obj/$(basename "$f").o"
    done
    g++ -o ffx_sc obj/*.o -pthread )
fi
[ -d "$work/shaders" ] || "$here/gen-ffx-shaders.sh" "$work/shaders"

