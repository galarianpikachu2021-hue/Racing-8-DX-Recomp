#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Packs the game program built on this machine into an AppImage for this machine (personal use; never distribute it:
# the program is a translation of the game's code, see LEGAL.md).
#   packaging/make-appimage.sh <game dir with mk8d and plugins/> <output dir>
# Needs linuxdeploy and appimagetool (fetched to ~/.cache/kartrecomp-tools). Saves, settings, mods, game files and keys
# live outside the AppImage, in the XDG folders (see AppRun).
set -euo pipefail
game="$(realpath "$1")"; out="$(realpath -m "$2")"
here="$(cd "$(dirname "$0")" && pwd)"
tools="${KARTRECOMP_TOOLS:-$HOME/.cache/kartrecomp-tools}"
mkdir -p "$tools"
for t in linuxdeploy appimagetool; do
  [ -x "$tools/$t-x86_64.AppImage" ] || {
    url=https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage
    [ $t = appimagetool ] && url=https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-x86_64.AppImage
    curl -fsSL -o "$tools/$t-x86_64.AppImage" "$url"; chmod +x "$tools/$t-x86_64.AppImage"; }
done
# Work files (the AppDir and tool logs) go to a cache folder; only the AppImage lands in <output dir>.
work="${XDG_CACHE_HOME:-$HOME/.cache}/kartrecomp-tools/work"
appdir="$work/AppDir"
rm -rf "$appdir"; mkdir -p "$appdir/usr/lib/kartrecomp/plugins" "$out"
echo "[1/3] Copying the game program and plugins"
cp "$game"/plugins/*.so "$appdir/usr/lib/kartrecomp/plugins/" 2>/dev/null || true
# linuxdeploy copies the program to usr/bin, bundles its libraries (minus the ones every system must provide:
# glibc, Vulkan loader, GPU drivers, audio server) and points its rpath at them.
export NO_STRIP=1
echo "[2/3] Bundling the libraries it needs"
"$tools/linuxdeploy-x86_64.AppImage" --appdir "$appdir" -e "$game/mk8d" -d "$here/kartrecomp.desktop" -i "$here/kartrecomp.png" >"$work/linuxdeploy.log" 2>&1 \
  || { tail -20 "$work/linuxdeploy.log"; exit 1; }
rm -f "$appdir/AppRun"; cp "$here/AppRun" "$appdir/AppRun"; chmod +x "$appdir/AppRun"
echo "[3/3] Compressing the AppImage (about 20 s)"
ARCH=x86_64 "$tools/appimagetool-x86_64.AppImage" --comp zstd "$appdir" "$out/KartRecomp-x86_64.AppImage" >"$work/appimagetool.log" 2>&1 \
  || { tail -20 "$work/appimagetool.log"; exit 1; }
ls -l "$out/KartRecomp-x86_64.AppImage"
