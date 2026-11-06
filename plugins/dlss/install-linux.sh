#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# install-linux.sh <plugin source dir> <plugins dir> [work dir]: builds the NVIDIA DLSS plugin on THIS PC and installs it.
# NVIDIA's SDK (github.com/NVIDIA/DLSS) is downloaded here, linked into the plugin here, and never leaves this PC: it is not part of any
# release (its licence forbids shipping it with open-source code). The launcher runs this from "Set up NVIDIA DLSS".
#   <plugin source dir>  holds kr_upscaler_dlss.cpp and kr_upscaler_abi.h (the AppImage carries them under usr/lib/kartrecomp/plugin-src/dlss)
#   <plugins dir>        where the engine looks for user plugins (~/.local/share/kartrecomp/plugins)
# Needs: g++ (or clang++), the Vulkan headers, curl or git, and an NVIDIA GPU with a current driver at run time.
set -euo pipefail
src="$(realpath "$1")"; dest="$(realpath -m "$2")"
work="$(realpath -m "${3:-${XDG_CACHE_HOME:-$HOME/.cache}/kartrecomp/dlss-build}")"
say() { printf '%s\n' "$*"; }
die() { printf 'ERROR: %s\n' "$*" >&2; exit 1; }
cxx="$(command -v g++ || command -v clang++ || true)"
[ -n "$cxx" ] || die "no C++ compiler found. Install one (Debian/Ubuntu: sudo apt install g++ ; Fedora: sudo dnf install gcc-c++ ; Arch: sudo pacman -S gcc), then try again."
[ -f /usr/include/vulkan/vulkan.h ] || [ -f /usr/local/include/vulkan/vulkan.h ] || die "the Vulkan headers are missing (Debian/Ubuntu: sudo apt install libvulkan-dev ; Fedora: sudo dnf install vulkan-headers vulkan-loader-devel ; Arch: sudo pacman -S vulkan-headers)."
[ -f "$src/kr_upscaler_dlss.cpp" ] || die "plugin source not found in $src"
mkdir -p "$work" "$dest"
sdk="$work/dlss"
if [ -d "$sdk/lib/Linux_x86_64" ]; then
  say "Using the DLSS SDK already downloaded in $sdk"
elif command -v git >/dev/null; then
  say "Downloading NVIDIA's DLSS SDK (github.com/NVIDIA/DLSS) to this PC ..."
  rm -rf "$sdk"; git clone --depth 1 https://github.com/NVIDIA/DLSS.git "$sdk"
elif command -v curl >/dev/null; then
  say "Downloading NVIDIA's DLSS SDK (github.com/NVIDIA/DLSS) to this PC ..."
  rm -rf "$sdk" "$work/dlss.tar.gz"; mkdir -p "$sdk"
  curl -fL https://github.com/NVIDIA/DLSS/archive/refs/heads/main.tar.gz -o "$work/dlss.tar.gz"
  tar -xzf "$work/dlss.tar.gz" -C "$sdk" --strip-components=1
else
  die "neither git nor curl is installed, so the SDK cannot be downloaded."
fi
[ -f "$sdk/lib/Linux_x86_64/libnvsdk_ngx.a" ] || die "the downloaded SDK has no lib/Linux_x86_64/libnvsdk_ngx.a (NVIDIA changed the layout?)"
say "Compiling the plugin with $cxx ..."
"$cxx" -std=c++17 -O2 -fPIC -Wall -Wno-unused-function -I"$sdk/include" -I"$src" -c "$src/kr_upscaler_dlss.cpp" -o "$work/plugin_dlss.o"
vk="-lvulkan"; ldconfig -p 2>/dev/null | grep -q 'libvulkan\.so ' || vk="-l:libvulkan.so.1"
"$cxx" -shared -o "$work/libkr_upscaler_dlss.so" "$work/plugin_dlss.o" -Wl,--start-group "$sdk/lib/Linux_x86_64/libnvsdk_ngx.a" -Wl,--end-group "$vk" -ldl -pthread
say "Installing into $dest ..."
cp -f "$work/libkr_upscaler_dlss.so" "$dest/"
cp -f "$sdk"/lib/Linux_x86_64/rel/libnvidia-ngx-dlss.so.* "$sdk"/lib/Linux_x86_64/rel/libnvidia-ngx-dlssg.so.* "$dest/" 2>/dev/null || cp -f "$sdk"/lib/Linux_x86_64/rel/libnvidia-ngx-dlss.so.* "$dest/"
ln -sf libkr_upscaler_dlss.so "$dest/libkr_framegen_dlss.so"   # the host finds frame generation by this name: same library
say "Done: DLSS is installed. Pick NVIDIA DLSS as the upscaler (launcher: Enhancements, or F10 in game)."
