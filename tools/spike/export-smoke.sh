#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# export-smoke.sh: kartrecomp-export on the user's real files. Passes when (1) the 3.0.4 export exits 0 and writes the four module
# folders with generated sources, (2) the 3.0.3 update (32-bit) exits 2 with a message that says so, (3) a missing key file exits 2.
# KARTRECOMP_EXPORT_BIN: the tool (default: the research workspace's clang build tree). Heavy: run under systemd-run --scope.
set -uo pipefail
cd "$(dirname "$0")/../.."
bin="${KARTRECOMP_EXPORT_BIN:-/home/carter/reverse-engineering/mk8d/mk8-recomp/build/suyu-nojit-clang/bin/kartrecomp-export}"
keys="${KARTRECOMP_KEYS:-$PWD/local/suyu-home/suyu/keys/prod.keys}"
base="$PWD/local/game/content/base.nsp"; upd="$PWD/local/game/content/update-3.0.4.nsp"
old="${KARTRECOMP_OLD_UPDATE:-/home/carter/reverse-engineering/mk8d/mk8-recomp/local/content/mk8d-update-3.0.3-v1245184.nsp}"
out="${1:-$PWD/local/spike/export-fresh}"
[ -x "$bin" ] || { echo "FAIL: no exporter at $bin"; exit 1; }
rm -rf "$out"
"$bin" --base "$base" --update "$upd" --keys "$keys" --out "$out" 2>"$out.err"; rc=$?
[ $rc -eq 0 ] || { echo "FAIL: 3.0.4 export exit $rc"; tail -5 "$out.err"; exit 1; }
for m in rtld main subsdk0 sdk; do ls "$out/$m"/src/*.c >/dev/null 2>&1 || { echo "FAIL: no sources for module $m"; exit 1; }; done
msg=$("$bin" --base "$base" --update "$old" --keys "$keys" --out "$out-old" 2>&1); rc=$?
[ $rc -eq 2 ] && echo "$msg" | grep -qi "32-bit" || { echo "FAIL: 3.0.3 update must exit 2 mentioning 32-bit (exit $rc): $msg"; exit 1; }
msg=$("$bin" --base "$base" --update "$upd" --keys /nonexistent/prod.keys --out "$out-nokeys" 2>&1); rc=$?
[ $rc -eq 2 ] && echo "$msg" | grep -qi "prod.keys" || { echo "FAIL: missing keys must exit 2 naming prod.keys (exit $rc): $msg"; exit 1; }
rm -rf "$out-old" "$out-nokeys"
# --info: one JSON line about an NSP (title id, version, 64-bit program), used by the launcher's Setup page
info=$("$bin" --info "$upd" --keys "$keys" 2>/dev/null)
echo "$info" | grep -q '"titleId":"0100152000022800"' && echo "$info" | grep -q '"version":1310720' && echo "$info" | grep -q '"aarch64":true' \
  || { echo "FAIL: --info on the 3.0.4 update: $info"; exit 1; }
info=$("$bin" --info "$old" --keys "$keys" 2>/dev/null)
echo "$info" | grep -q '"aarch64":false' && echo "$info" | grep -q '"version":1245184' || { echo "FAIL: --info on the 3.0.3 update: $info"; exit 1; }
info=$("$bin" --info "$base" --keys "$keys" 2>/dev/null)
echo "$info" | grep -q '"titleId":"0100152000022000"' && echo "$info" | grep -q '"version":0' || { echo "FAIL: --info on the base game: $info"; exit 1; }
echo PASS
