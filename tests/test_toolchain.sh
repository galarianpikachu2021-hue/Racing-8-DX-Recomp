#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# test_toolchain.sh [toolchain dir]: the bundled toolchain compiles and links a module-style shared object with NO system compiler
# on PATH, the result needs no glibc newer than 2.31 (so it loads on the distros the AppImage targets) and loads and runs.
set -uo pipefail
cd "$(dirname "$0")/.."
T="$(realpath "${1:-${KARTRECOMP_TOOLCHAIN:-local/toolchain}}")"
[ -x "$T/bin/clang" ] && [ -d "$T/sysroot/usr/include" ] || { echo "FAIL: no toolchain in $T"; exit 1; }
tmp="$(mktemp -d)"; trap 'rm -rf "$tmp"' EXIT
cat > "$tmp/m.c" <<'EOT'
#include <math.h>
#include <stdint.h>
#include <string.h>
__attribute__((visibility("default"))) double module_probe(double x) { char b[8]; uint64_t v = 7; memcpy(b, &v, 8); return fmod(x, 3.0) + sqrt(x) + (double)b[0]; }
__int128 __attribute__((visibility("default"))) wide(uint64_t a, uint64_t b) { return (__int128)a * b; }  /* needs compiler-rt builtins */
EOT
run() { env -i PATH=/nonexistent HOME="$tmp" "$@"; }
run "$T/bin/clang" --target=x86_64-linux-gnu --sysroot="$T/sysroot" -std=gnu11 -O2 -march=x86-64-v3 -fPIC -fvisibility=hidden -c "$tmp/m.c" -o "$tmp/m.o" \
  || { echo "FAIL: compile"; exit 1; }
run "$T/bin/clang" --target=x86_64-linux-gnu --sysroot="$T/sysroot" -fuse-ld=lld -nostdlib -shared -rtlib=compiler-rt -o "$tmp/m.so" "$tmp/m.o" \
  -L"$T/sysroot/lib/x86_64-linux-gnu" -L"$T/sysroot/usr/lib/x86_64-linux-gnu" -lm -lc "$T/lib/libclang_rt.builtins-x86_64.a" \
  || { echo "FAIL: link"; exit 1; }
newest=$(objdump -T "$tmp/m.so" | grep -oE 'GLIBC_[0-9.]+' | sort -V | tail -1)
[ -n "$newest" ] && [ "$(printf '%s\nGLIBC_2.31\n' "$newest" | sort -V | tail -1)" = GLIBC_2.31 ] || { echo "FAIL: needs $newest, more than GLIBC_2.31"; exit 1; }
out=$(python3 -I -c "import ctypes;l=ctypes.CDLL('$tmp/m.so');l.module_probe.restype=ctypes.c_double;l.module_probe.argtypes=[ctypes.c_double];print(round(l.module_probe(10.0),3))") \
  || { echo "FAIL: load/run"; exit 1; }
[ "$out" = "11.162" ] || { echo "FAIL: wrong result $out (wanted 11.162)"; exit 1; }
echo PASS
