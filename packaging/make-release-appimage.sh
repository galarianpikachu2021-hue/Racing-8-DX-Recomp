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
bad=$(find "$appdir" -type f \( -iname '*.nsp' -o -iname '*.xci' -o -iname '*.nca' -o -iname 'prod.keys*' -o -iname 'title.keys*' -o -name 'recompiled_*.c' -o -name 'recompiled_*.so' \) | head -5)
[ -z "$bad" ] || { echo "FAIL: game-derived files in the AppDir: $bad"; exit 1; }
echo "[4/4] Compressing the AppImage"
ARCH=x86_64 "$tools/appimagetool-x86_64.AppImage" --comp zstd "$appdir" "$out/KartRecomp-Launcher-x86_64.AppImage" >"$work/appimagetool.log" 2>&1 \
  || { tail -20 "$work/appimagetool.log"; exit 1; }
ls -l "$out/KartRecomp-Launcher-x86_64.AppImage"
