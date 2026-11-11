#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# build-release-ubuntu2204.sh <fork source> <out dir>: builds the Linux release AppImage inside an Ubuntu 22.04 container (podman), so
# every bundled program and library needs glibc 2.35 at most and the AppImage starts on Ubuntu 22.04+, Fedora, SteamOS and other
# current distros (GitHub issues #13, #15; the AppImage catalog tests on the oldest supported Ubuntu LTS).
#
#   <fork source>  the suyu fork with patches/suyu applied (packaging/windows/fetch-fork.sh <dir> makes one)
#   <out dir>      build trees, Qt, the dependency cache and the finished AppImage (out/Racing_8_DX_Recomp-x86_64.AppImage)
#
# Inside the container: g++-12; the engine's dependencies are fetched and built by the fork's CPM (CPMUTIL_FORCE_BUNDLED), not taken
# from the system. SDL3 and OpenSSL are built from source: their prebuilt CI archives need glibc 2.38 (__isoc23_sscanf); FFmpeg too (externals, against the system libva: the CI build needs libva 2.21+); the launcher uses Qt 6.7.3 from Qt's own binaries (aqtinstall), which
# also target old glibc. The bundled game compiler (local/toolchain, packaging/make-toolchain.sh) is already built on a glibc 2.31 base.
# Engine version: KARTRECOMP_ENGINE_VERSION (default: the patch hash, as packaging/build-engine.sh).
set -euo pipefail
fork="$(realpath "$1")"; out="$(realpath -m "$2")"
repo="$(cd "$(dirname "$0")/../.." && pwd)"
tools="${KARTRECOMP_TOOLS:-$HOME/.cache/kartrecomp-tools}"   # linuxdeploy, its Qt plugin, appimagetool (make-launcher-appimage.sh fetches them)
name="${KARTRECOMP_CONTAINER:-kr235}"
version="${KARTRECOMP_ENGINE_VERSION:-$(cat "$repo"/patches/suyu/*.patch | sha256sum | cut -c1-10)-abi6}"
jobs="${JOBS:-5}"
mkdir -p "$out"

if ! podman container exists "$name"; then
  # --network=host: pasta networking needs /dev/net/tun, which some kernels lack.
  podman run -d --network=host --name "$name" -v "$fork:/work/fork:ro" -v "$repo:/work/repo" -v "$out:/work/out" -v "$tools:/work/tools:ro" \
    docker.io/library/ubuntu:22.04 sleep infinity >/dev/null
fi
podman start "$name" >/dev/null
run() { podman exec "$name" bash -c "$1"; }

echo "[1/5] Build tools in the container"
run 'command -v g++-12 >/dev/null && command -v cmake >/dev/null || {
  export DEBIAN_FRONTEND=noninteractive
  apt-get update -qq && apt-get install -y -qq --no-install-recommends build-essential g++-12 gcc-12 git curl wget ca-certificates python3 \
    python3-pip pkg-config ninja-build nasm yasm unzip zip xz-utils file patchelf glslang-tools autoconf automake libtool \
    libusb-1.0-0-dev libudev-dev libasound2-dev libpulse-dev libpipewire-0.3-dev libx11-dev libxext-dev libxrandr-dev libxcursor-dev \
    libxi-dev libxss-dev libxkbcommon-dev libwayland-dev wayland-protocols libdecor-0-dev libegl1-mesa-dev libgl1-mesa-dev \
    libgles2-mesa-dev libdbus-1-dev libvulkan-dev libssl-dev zlib1g-dev libbrotli-dev liblz4-dev libzstd-dev libopus-dev libenet-dev \
    libfmt-dev libdrm-dev libva-dev libvdpau-dev libxtst-dev libxfixes-dev libxinerama-dev libxrender-dev \
    libxkbcommon-x11-dev libibus-1.0-dev libsndio-dev libjack-dev libxcb1-dev libxcb-cursor0 libxcb-xinerama0 libfontconfig1 libfreetype6 \
    libxkbcommon-x11-0 libxcb-icccm4 libxcb-image0 libxcb-keysyms1 libxcb-randr0 libxcb-render-util0 libxcb-shape0 libgl1 >/dev/null
  pip3 install -q cmake==3.31.6 aqtinstall==3.3.0
}'

echo "[2/5] Qt 6.7.3"
run '[ -d /work/out/qt/6.7.3/gcc_64 ] || aqt install-qt linux desktop 6.7.3 linux_gcc_64 -O /work/out/qt >/work/out/aqt.log'

echo "[3/5] Engine and exporter (version $version)"
run "cd /work/out && CC=gcc-12 CXX=g++-12 cmake -S /work/fork -B eng -GNinja -DCMAKE_BUILD_TYPE=Release -DCPM_SOURCE_CACHE=/work/out/cpm -DCMAKE_LINKER:FILEPATH=/usr/bin/gcc-12 "-DCMAKE_CXX_STANDARD_LIBRARIES=-lva-drm -lva-x11 -lva -lvdpau -ldrm -lX11" \
  -DENABLE_QT=OFF -DYUZU_TESTS=OFF -DENABLE_WEB_SERVICE=OFF -DUSE_DISCORD_PRESENCE=OFF -DYUZU_ROOM=OFF -DYUZU_ROOM_STANDALONE=OFF \
  -DSUYU_NO_JIT=ON -DKARTRECOMP_ENGINE=ON -DKARTRECOMP_ENGINE_VERSION=$version -DCPMUTIL_FORCE_BUNDLED=ON \
  -DYUZU_USE_BUNDLED_FFMPEG=OFF -DYUZU_USE_EXTERNAL_FFMPEG=ON -DYUZU_USE_BUNDLED_SDL3=OFF -DYUZU_USE_BUNDLED_OPENSSL=OFF >/work/out/cfg.log && \
  nice ninja -C eng -j$jobs mk8d-engine kartrecomp-export >/work/out/build.log"
run "mkdir -p /work/out/engine-dir/engine/plugins && cp /work/out/eng/bin/mk8d-engine /work/out/engine-dir/engine/ && strip -s /work/out/engine-dir/engine/mk8d-engine \
  && echo $version > /work/out/engine-dir/engine/ENGINE_VERSION && cp /work/out/eng/bin/kartrecomp-export /work/out/kartrecomp-export && strip -s /work/out/kartrecomp-export"
# FSR 3 plugins (needs glibc 2.29 and GLIBCXX 3.4.21 only; built by plugins/fsr3/build.sh on the host is fine)
for d in "${KARTRECOMP_PLUGINS_DIR:-}" "$HOME/.cache/kartrecomp-fsr3" "$repo/local/game/plugins"; do
  [ -n "$d" ] && [ -f "$d/libkr_upscaler_fsr3.so" ] && { cp "$d/libkr_upscaler_fsr3.so" "$out/engine-dir/engine/plugins/"; ln -sf libkr_upscaler_fsr3.so "$out/engine-dir/engine/plugins/libkr_framegen_fsr3.so"; break; }
done

echo "[4/5] Launcher (Qt 6.7.3)"
run "cd /work/out && CC=gcc-12 CXX=g++-12 cmake -S /work/repo -B launcher -GNinja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=/work/out/qt/6.7.3/gcc_64 >/work/out/lcfg.log \
  && nice ninja -C launcher -j$jobs >/work/out/lbuild.log && cd launcher && QT_QPA_PLATFORM=offscreen LD_LIBRARY_PATH=/work/out/qt/6.7.3/gcc_64/lib ctest --output-on-failure >/work/out/ltest.log"

echo "[5/5] AppImage (the glibc floor check stays on: 2.35)"
run "cd /work/repo && KARTRECOMP_TOOLS=/work/tools QMAKE=/work/out/qt/6.7.3/gcc_64/bin/qmake XDG_CACHE_HOME=/work/out/cache \
  LD_LIBRARY_PATH=/work/out/qt/6.7.3/gcc_64/lib packaging/make-release-appimage.sh /work/out/launcher /work/out/engine-dir /work/out/kartrecomp-export local/toolchain /work/out/release"
(cd "$out/release" && sha256sum Racing_8_DX_Recomp-x86_64.AppImage > Racing_8_DX_Recomp-x86_64.AppImage.sha256)
ls -l "$out/release"
