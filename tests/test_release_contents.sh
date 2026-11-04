#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# test_release_contents.sh <AppImage>: what the release holds. The FSR 3 plugins ship (MIT), the DLSS ones never do (NVIDIA's licence), no file
# derived from a game is inside, and the glibc floor is reported (and compared with KARTRECOMP_MAX_GLIBC when that is set).
set -uo pipefail
cd "$(dirname "$0")/.."
img="$(realpath "${1:-}" 2>/dev/null)" || { echo "usage: $0 <AppImage>"; exit 1; }
tmp="$(mktemp -d -p "${TMPDIR:-/tmp}")"; trap 'rm -rf "$tmp"' EXIT
(cd "$tmp" && "$img" --appimage-extract >/dev/null 2>&1) || { echo "FAIL: cannot unpack the AppImage"; exit 1; }
root="$tmp/squashfs-root"
for p in libkr_upscaler_fsr3.so libkr_framegen_fsr3.so; do [ -e "$root/usr/bin/plugins/$p" ] || { echo "FAIL: $p is not in the release"; exit 1; }; done
find "$root" -iname '*dlss*' -o -iname '*ngx*' | grep -q . && { echo "FAIL: DLSS/NGX files are in the release"; exit 1; }
bad=$(find "$root" -type f \( -iname '*.nsp' -o -iname '*.xci' -o -iname '*.nca' -o -iname '*.nso' -o -iname '*.npdm' -o -iname 'prod.keys*' -o -iname 'title.keys*' -o -name 'recomp_*.c' -o -name 'recompiled_*' -o -name 'module.json' \) -o \( -name '*.o' -not -path '*/toolchain/*' \) | head -3)
[ -z "$bad" ] || { echo "FAIL: game-derived files in the release: $bad"; exit 1; }
[ -f "$root/usr/bin/ENGINE_VERSION" ] || { echo "FAIL: no ENGINE_VERSION"; exit 1; }
packaging/glibc-floor.sh "$root" "${KARTRECOMP_MAX_GLIBC:-}" || { echo "FAIL: glibc floor too high"; exit 1; }
echo PASS
