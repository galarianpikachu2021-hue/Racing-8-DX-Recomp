#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Builds plugins/libkr_upscaler_dlss.so: the NVIDIA DLSS upscaler plugin, through NGX. The SDK (github.com/NVIDIA/DLSS) is fetched into a work
# folder and linked statically; nothing from it is committed here, and the built library must stay local (the SDK's license forbids
# redistributing it under an open-source license). Runs only on an NVIDIA GPU with a current driver.
#
#   plugins/dlss/build.sh [work dir]        (default: ~/.cache/kartrecomp-dlss)
# Needs: git, g++, vulkan headers. Output: <work dir>/libkr_upscaler_dlss.so, next to a copy of the DLSS runtime snippets
# (libnvidia-ngx-dlss.so.*), which the plugin finds in its own folder: copy all of them into local/game/plugins/.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
work="$(realpath -m "${1:-$HOME/.cache/kartrecomp-dlss}")"
mkdir -p "$work"
[ -d "$work/dlss" ] || git clone --depth 1 https://github.com/NVIDIA/DLSS.git "$work/dlss"
sdk="$work/dlss"
g++ -std=c++17 -O2 -fPIC -Wall -Wno-unused-function -I"$sdk/include" -I"$here" -c "$here/kr_upscaler_dlss.cpp" -o "$work/plugin_dlss.o"
g++ -shared -o "$work/libkr_upscaler_dlss.so" "$work/plugin_dlss.o" -Wl,--start-group "$sdk/lib/Linux_x86_64/libnvsdk_ngx.a" -Wl,--end-group -lvulkan -ldl -pthread
cp -f "$sdk"/lib/Linux_x86_64/rel/libnvidia-ngx-dlss.so.* "$sdk"/lib/Linux_x86_64/rel/libnvidia-ngx-dlssg.so.* "$work/"
ln -sf libkr_upscaler_dlss.so "$work/libkr_framegen_dlss.so"   # the host finds frame generation by this name; same library
echo "built $work/libkr_upscaler_dlss.so + libkr_framegen_dlss.so link (+ DLSS runtime snippets: super resolution and frame generation)"
