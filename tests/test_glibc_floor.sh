#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# test_glibc_floor.sh: packaging/glibc-floor.sh reports the glibc a program needs and enforces a maximum.
set -uo pipefail
cd "$(dirname "$0")/.."
tmp="$(mktemp -d)"; trap 'rm -rf "$tmp"' EXIT
cat > "$tmp/t.c" <<'EOT'
#include <math.h>
#include <string.h>
int main(int c, char** v) { char b[8]; memcpy(b, v[0], 1); return (int)fmod(c, 3.0) + b[0]; }
EOT
cc -o "$tmp/t" "$tmp/t.c" -lm || { echo "FAIL: no C compiler for the fixture"; exit 1; }
out=$(packaging/glibc-floor.sh "$tmp") || { echo "FAIL: the floor script failed: $out"; exit 1; }
echo "$out" | grep -qE '^glibc [0-9]+\.[0-9]+' || { echo "FAIL: no version in: $out"; exit 1; }
packaging/glibc-floor.sh "$tmp" 99 >/dev/null || { echo "FAIL: a high maximum must pass"; exit 1; }
packaging/glibc-floor.sh "$tmp" 1.0 >/dev/null 2>&1 && { echo "FAIL: a maximum of 1.0 must fail"; exit 1; }
echo PASS
