#!/usr/bin/env bash
# Generates the FSR 3.1 upscaler, frame interpolation and optical flow shader blobs for the Vulkan backend with the Linux port of FidelityFX-SC (ffxsc/).
set -euo pipefail
here="${FSR3_WORK:?set FSR3_WORK}"
sdk="$here/sdk/sdk"
out="${1:-$here/shaders}"
mkdir -p "$out"
SC="$here/ffxsc/ffx_sc"
GPU="$sdk/include/FidelityFX/gpu"
BASE=(-reflection -deps=gcc -DFFX_GPU=1
  -DFFX_FSR3UPSCALER_OPTION_UPSAMPLE_SAMPLERS_USE_DATA_HALF=0
  -DFFX_FSR3UPSCALER_OPTION_ACCUMULATE_SAMPLERS_USE_DATA_HALF=0
  -DFFX_FSR3UPSCALER_OPTION_REPROJECT_SAMPLERS_USE_DATA_HALF=1
  -DFFX_FSR3UPSCALER_OPTION_POSTPROCESSLOCKSTATUS_SAMPLERS_USE_DATA_HALF=0
  -DFFX_FSR3UPSCALER_OPTION_UPSAMPLE_USE_LANCZOS_TYPE=2)
API=(-compiler=glslang -glslangexe=/usr/bin/glslangValidator -e CS --target-env vulkan1.2 -S comp -Os -DFFX_GLSL=1)
PERM=(-DFFX_FSR3UPSCALER_OPTION_REPROJECT_USE_LANCZOS_TYPE='{0,1}'
  -DFFX_FSR3UPSCALER_OPTION_HDR_COLOR_INPUT='{0,1}'
  -DFFX_FSR3UPSCALER_OPTION_LOW_RESOLUTION_MOTION_VECTORS='{0,1}'
  -DFFX_FSR3UPSCALER_OPTION_JITTERED_MOTION_VECTORS='{0,1}'
  -DFFX_FSR3UPSCALER_OPTION_INVERTED_DEPTH='{0,1}'
  -DFFX_FSR3UPSCALER_OPTION_APPLY_SHARPENING='{0,1}')
INC=(-I"$GPU" -I"$GPU/fsr3upscaler")
if [ -z "${ONLY_FG:-}" ]; then   # ONLY_FG=1: just frame interpolation and optical flow
for f in "$sdk"/src/backends/vk/shaders/fsr3upscaler/*.glsl; do
  nm="$(basename "$f" .glsl)"
  for v in "" _wave64 _16bit _wave64_16bit; do
    half=0; [[ "$v" == *16bit* ]] && half=1
    echo "== $nm$v"
    "$SC" "${BASE[@]}" "${API[@]}" "${PERM[@]}" "${INC[@]}" -name="$nm$v" -DFFX_HALF=$half -output="$out" "$f" >/dev/null
  done
done
fi

# Frame interpolation and optical flow (the FSR 3 frame-generation backend).
FI_BASE=(-reflection -deps=gcc -DFFX_GPU=1
  -DFFX_FRAMEINTERPOLATION_OPTION_UPSAMPLE_SAMPLERS_USE_DATA_HALF=0
  -DFFX_FRAMEINTERPOLATION_OPTION_ACCUMULATE_SAMPLERS_USE_DATA_HALF=0
  -DFFX_FRAMEINTERPOLATION_OPTION_REPROJECT_SAMPLERS_USE_DATA_HALF=1
  -DFFX_FRAMEINTERPOLATION_OPTION_POSTPROCESSLOCKSTATUS_SAMPLERS_USE_DATA_HALF=0
  -DFFX_FRAMEINTERPOLATION_OPTION_UPSAMPLE_USE_LANCZOS_TYPE=2)
FI_PERM=(-DFFX_FRAMEINTERPOLATION_OPTION_LOW_RES_MOTION_VECTORS='{0,1}'
  -DFFX_FRAMEINTERPOLATION_OPTION_JITTER_MOTION_VECTORS='{0,1}'
  -DFFX_FRAMEINTERPOLATION_OPTION_INVERTED_DEPTH='{0,1}')
for f in "$sdk"/src/backends/vk/shaders/frameinterpolation/*.glsl; do
  nm="$(basename "$f" .glsl)"
  for v in "" _wave64 _16bit _wave64_16bit; do
    half=0; [[ "$v" == *16bit* ]] && half=1
    echo "== $nm$v"
    "$SC" "${FI_BASE[@]}" "${API[@]}" "${FI_PERM[@]}" -I"$GPU" -I"$GPU/frameinterpolation" -name="$nm$v" -DFFX_HALF=$half -output="$out" "$f" >/dev/null
  done
done
OF_BASE=(-reflection -deps=gcc -DFFX_GPU=1)
OF_PERM=(-DFFX_OPTICALFLOW_OPTION_HDR_COLOR_INPUT='{0,1}')
for f in "$sdk"/src/backends/vk/shaders/opticalflow/*.glsl; do
  nm="$(basename "$f" .glsl)"
  for v in "" _wave64 _16bit _wave64_16bit; do
    half=0; [[ "$v" == *16bit* ]] && half=1
    echo "== $nm$v"
    "$SC" "${OF_BASE[@]}" "${API[@]}" "${OF_PERM[@]}" -I"$GPU" -I"$GPU/opticalflow" -name="$nm$v" -DFFX_HALF=$half -output="$out" "$f" >/dev/null
  done
done
