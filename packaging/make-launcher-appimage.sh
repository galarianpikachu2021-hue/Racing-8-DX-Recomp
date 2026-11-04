#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Packs the Qt launcher (no game, no game code) into a redistributable AppImage; this is the file attached to GitHub releases.
#   packaging/make-launcher-appimage.sh <built launcher binary, e.g. build/app/kartrecomp> <output dir>
# Needs linuxdeploy, its Qt plugin and appimagetool (fetched to ~/.cache/kartrecomp-tools) and Qt 6 (qmake6) on the build machine.
set -euo pipefail
bin="$(realpath "$1")"; out="$(realpath -m "$2")"
here="$(cd "$(dirname "$0")" && pwd)"
tools="${KARTRECOMP_TOOLS:-$HOME/.cache/kartrecomp-tools}"
mkdir -p "$tools" "$out"
fetch() {  # <file name> <url>
  [ -x "$tools/$1" ] || { curl -fsSL -o "$tools/$1" "$2"; chmod +x "$tools/$1"; }
}
fetch linuxdeploy-x86_64.AppImage https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage
fetch linuxdeploy-plugin-qt-x86_64.AppImage https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage
fetch appimagetool-x86_64.AppImage https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-x86_64.AppImage
work="${XDG_CACHE_HOME:-$HOME/.cache}/kartrecomp-tools/work-launcher"
appdir="$work/AppDir"
rm -rf "$appdir"; mkdir -p "$appdir"
export NO_STRIP=1 QMAKE="${QMAKE:-$(command -v qmake6 || command -v qmake)}" LD_LIBRARY_PATH="$tools:${LD_LIBRARY_PATH:-}"
export PATH="$tools:$PATH" EXTRA_PLATFORM_PLUGINS="${EXTRA_PLATFORM_PLUGINS:-libqwayland.so}" EXTRA_QT_PLUGINS="${EXTRA_QT_PLUGINS:-wayland-decoration-client;wayland-graphics-integration-client;wayland-shell-integration}" APPIMAGE_EXTRACT_AND_RUN=1
echo "[1/2] Bundling Qt and the launcher's libraries"
"$tools/linuxdeploy-x86_64.AppImage" --appdir "$appdir" -e "$bin" -d "$here/kartrecomp-launcher.desktop" -i "$here/kartrecomp.png" --plugin qt \
  >"$work/linuxdeploy.log" 2>&1 || { tail -25 "$work/linuxdeploy.log"; exit 1; }
echo "[2/2] Compressing the AppImage"
ARCH=x86_64 "$tools/appimagetool-x86_64.AppImage" --comp zstd "$appdir" "$out/KartRecomp-Launcher-x86_64.AppImage" >"$work/appimagetool.log" 2>&1 \
  || { tail -20 "$work/appimagetool.log"; exit 1; }
ls -l "$out/KartRecomp-Launcher-x86_64.AppImage"
