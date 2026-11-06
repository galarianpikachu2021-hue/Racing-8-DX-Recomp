#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# make-release-appimage.sh <launcher build dir> <engine dir> <exporter binary> <toolchain dir> <out dir>
# The release AppImage: launcher, Setup/build tools, the prebuilt engine, the exporter, the bundled compiler. Everything needed to turn
# the user's own game files into the game, and nothing derived from a game (checked below).
#   launcher build dir: has app/kartrecomp, app/kartrecomp-setup, app/kartrecomp-build
#   engine dir:         output of packaging/build-engine.sh (engine/mk8d-engine, ENGINE_VERSION, plugins/)
#   toolchain dir:      output of packaging/make-toolchain.sh
set -euo pipefail
b="$(realpath "$1")"; eng="$(realpath "$2")"; exporter="$(realpath "$3")"; tc="$(realpath "$4")"; out="$(realpath -m "$5")"
here="$(cd "$(dirname "$0")" && pwd)"; repo="$here/.."
tools="${KARTRECOMP_TOOLS:-$HOME/.cache/kartrecomp-tools}"
mkdir -p "$tools" "$out"
for t in linuxdeploy linuxdeploy-plugin-qt appimagetool; do
  [ -x "$tools/$t-x86_64.AppImage" ] || { echo "run packaging/make-launcher-appimage.sh once first (it fetches $t)"; exit 1; }
done
work="${XDG_CACHE_HOME:-$HOME/.cache}/kartrecomp-tools/work-release"
appdir="$work/AppDir"
rm -rf "$appdir"; mkdir -p "$appdir/usr/bin/plugins" "$appdir/usr/lib/kartrecomp/data/mk8d-3.0.4"
export NO_STRIP=1 QMAKE="${QMAKE:-$(command -v qmake6 || command -v qmake)}" LD_LIBRARY_PATH="$tools:${LD_LIBRARY_PATH:-}" APPIMAGE_EXTRACT_AND_RUN=1
export PATH="$tools:$PATH" EXTRA_PLATFORM_PLUGINS="libqwayland.so" EXTRA_QT_PLUGINS="wayland-decoration-client;wayland-graphics-integration-client;wayland-shell-integration"
echo "[1/4] Bundling the programs and their libraries"
"$tools/linuxdeploy-x86_64.AppImage" --appdir "$appdir" -e "$b/app/kartrecomp" -e "$b/app/kartrecomp-setup" -e "$b/app/kartrecomp-build" \
  -e "$eng/engine/mk8d-engine" -e "$exporter" -d "$here/kartrecomp-launcher.desktop" -i "$here/kartrecomp.png" --plugin qt \
  >"$work/linuxdeploy.log" 2>&1 || { tail -25 "$work/linuxdeploy.log"; exit 1; }
cp "$eng/engine/ENGINE_VERSION" "$appdir/usr/bin/"
cp "$eng"/engine/plugins/* "$appdir/usr/bin/plugins/" 2>/dev/null || true
echo "[2/4] Adding the compiler and the hot block list"
cp -a "$tc" "$appdir/usr/lib/kartrecomp/toolchain"
cp "$repo/data/mk8d-3.0.4/hot_blocks.txt" "$appdir/usr/lib/kartrecomp/data/mk8d-3.0.4/"
rm -f "$appdir/AppRun"; cp "$here/AppRun.release" "$appdir/AppRun"; chmod +x "$appdir/AppRun"
echo "[3/4] Checking that nothing from a game is inside"
bad=$(find "$appdir" -type f \( -iname '*.nsp' -o -iname '*.nsz' -o -iname '*.xci' -o -iname '*.xcz' -o -iname '*.nca' -o -iname '*.nso' -o -iname '*.npdm' -o -iname 'prod.keys*' -o -iname 'title.keys*' \
  -o -name 'recomp_*.c' -o -name 'recompiled_*' -o -name 'module.json' \) -o \( -name '*.o' -not -path '*/toolchain/*' \) | head -5)
[ -z "$bad" ] || { echo "FAIL: game-derived files in the AppDir: $bad"; exit 1; }
[ -e "$appdir/usr/bin/plugins/libkr_upscaler_fsr3.so" ] || echo "WARNING: no FSR 3 plugin in the release" >&2
# The DLSS plugin SOURCE (ours) ships so the user can build the plugin on their own PC; nothing of NVIDIA's does.
mkdir -p "$appdir/usr/lib/kartrecomp/plugin-src/dlss"
cp "$repo/plugins/dlss/install-linux.sh" "$repo/plugins/dlss/kr_upscaler_dlss.cpp" "$repo/plugins/dlss/kr_upscaler_abi.h" "$appdir/usr/lib/kartrecomp/plugin-src/dlss/"
find "$appdir" \( -iname '*dlss*' -o -iname '*ngx*' \) -not -path '*/plugin-src/dlss' -not -path '*/plugin-src/dlss/*' | grep -q . && { echo "FAIL: DLSS/NGX files must never ship"; exit 1; }
# The AppImage must start on common distros: refuse a release that needs a newer glibc than KARTRECOMP_MAX_GLIBC (default 2.35), unless
# KARTRECOMP_ALLOW_NEW_GLIBC=1 (a local build on a new distro; build the engine on an older base for a real release).
if [ -n "${KARTRECOMP_ALLOW_NEW_GLIBC:-}" ]; then "$here/glibc-floor.sh" "$appdir" || true
else "$here/glibc-floor.sh" "$appdir" "${KARTRECOMP_MAX_GLIBC:-2.35}" || { echo "FAIL: the release needs a newer glibc than common distros ship: build the engine on an older base"; exit 1; }; fi
echo "[4/4] Compressing the AppImage"
ARCH=x86_64 "$tools/appimagetool-x86_64.AppImage" --comp zstd "$appdir" "$out/KartRecomp-Launcher-x86_64.AppImage" >"$work/appimagetool.log" 2>&1 \
  || { tail -20 "$work/appimagetool.log"; exit 1; }
ls -l "$out/KartRecomp-Launcher-x86_64.AppImage"
