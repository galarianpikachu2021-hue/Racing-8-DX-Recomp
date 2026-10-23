#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Builds plugins/libkr_upscaler_fsr3.so: the FSR 3.1 upscaler plugin (and FSR 3 frame generation, in the same library), from AMD's FidelityFX SDK v1.1.4 (MIT license; the last release with a
# Vulkan backend). Everything third-party is fetched into a work folder and built there; nothing from the SDK is committed here.
#
#   plugins/fsr3/build.sh [work dir]        (default: ~/.cache/kartrecomp-fsr3)
# Needs: git, g++, glslangValidator, python3, vulkan headers + loader. Output: <work dir>/libkr_upscaler_fsr3.so
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
work="$(realpath -m "${1:-$HOME/.cache/kartrecomp-fsr3}")"
mkdir -p "$work"
export FSR3_WORK="$work"
jobs="${JOBS:-4}"

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
if [ ! -x "$work/ffxsc/ffx_sc" ]; then
  python3 "$here/port-ffx-sc.py" "$sdk/tools/ffx_shader_compiler" "$work/ffxsc"
  ( cd "$work/ffxsc" && mkdir -p obj
    SCINC=(-Isrc -Ilibs/SPIRV-Reflect -Ilibs/SPIRV-Reflect/include -Ilibs/tiny-process-library -Ilibs/MD5)
    gcc -c -O1 "${SCINC[@]}" libs/SPIRV-Reflect/spirv_reflect.c -o obj/spirv_reflect.o
    for f in src/ffx_sc.cpp src/glsl_compiler.cpp src/utils.cpp libs/MD5/md5.cpp libs/tiny-process-library/process.cpp libs/tiny-process-library/process_unix.cpp; do
      g++ -std=c++17 -O1 -w -c "${SCINC[@]}" "$f" -o "obj/$(basename "$f").o"
    done
    g++ -o ffx_sc obj/*.o -pthread )
fi
[ -d "$work/shaders" ] || "$here/gen-ffx-shaders.sh" "$work/shaders"

# 3. The SDK's FSR 3 upscaler + Vulkan backend, and our wrapper.
VKINC="${VULKAN_INCLUDE:-/usr/include}"
INC=(-I"$sdk/include" -I"$sdk/src/shared" -I"$sdk/src/backends/shared" -I"$sdk/src/backends/shared/blob_accessors" -I"$sdk/src/components" -I"$sdk/src/backends/vk" -I"$work/shaders" -I"$VKINC")
CXX=(g++ -std=c++17 -O2 -fPIC -w -include "$here/linux_shim.h" -DFFX_FSR3UPSCALER=1 -DFFX_FI=1 -DFFX_OF=1 -DFFX_API_VK=1)
mkdir -p "$work/obj"
srcs=("$sdk/src/components/fsr3upscaler/ffx_fsr3upscaler.cpp" "$sdk/src/backends/vk/ffx_vk.cpp" "$sdk/src/shared/ffx_assert.cpp"
      "$sdk/src/components/frameinterpolation/ffx_frameinterpolation.cpp" "$sdk/src/components/opticalflow/ffx_opticalflow.cpp"
      "$sdk/src/backends/shared/blob_accessors/ffx_frameinterpolation_shaderblobs.cpp" "$sdk/src/backends/shared/blob_accessors/ffx_opticalflow_shaderblobs.cpp"
      "$sdk/src/shared/ffx_message.cpp" "$sdk/src/shared/ffx_breadcrumbs_list.cpp" "$sdk/src/shared/ffx_object_management.cpp"
      "$sdk/src/backends/shared/ffx_shader_blobs.cpp" "$sdk/src/backends/shared/blob_accessors/ffx_fsr3upscaler_shaderblobs.cpp")
pids=()
for f in "${srcs[@]}"; do
  "${CXX[@]}" "${INC[@]}" -c "$f" -o "$work/obj/$(basename "$f" .cpp).o" &
  pids+=($!)
  if [ "${#pids[@]}" -ge "$jobs" ]; then wait "${pids[0]}"; pids=("${pids[@]:1}"); fi
done
wait
"${CXX[@]}" -I"$sdk/include" -I"$VKINC" -I"$here" -c "$here/kr_upscaler_fsr3.cpp" -o "$work/obj/plugin.o"
g++ -shared -o "$work/libkr_upscaler_fsr3.so" "$work/obj"/*.o -Wl,--no-undefined -lvulkan -ldl -pthread
# The frame-generation backend lives in the same library (it shares the upscaler's images); the host loads it under its own name,
# and a symlink makes the dynamic loader hand both names the same mapping.
ln -sf libkr_upscaler_fsr3.so "$work/libkr_framegen_fsr3.so"
echo "built $work/libkr_upscaler_fsr3.so and the libkr_framegen_fsr3.so link (copy both to local/game/plugins/, keeping the link)"
