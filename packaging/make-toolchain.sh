#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# make-toolchain.sh <out dir>: assembles the compiler the builder ships with: clang + lld + compiler-rt builtins from the official
# LLVM 20.1.8 release (Apache-2.0 with LLVM exceptions) and a minimal glibc 2.31 sysroot from Debian 11 packages, so the game
# modules need no glibc newer than 2.31 and the user's machine needs no compiler or dev packages. Downloads are checksum-pinned and kept
# in ~/.cache/kartrecomp-tools/dl. Result: <out>/{bin/clang,bin/ld.lld,lib/clang/20/include,lib/libclang_rt.builtins-x86_64.a,sysroot,TOOLCHAIN_VERSION}.
set -euo pipefail
out="$(realpath -m "$1")"
dl="${KARTRECOMP_TOOLS:-$HOME/.cache/kartrecomp-tools}/dl"
mkdir -p "$dl"
fetch() {  # <file> <url> <sha256>
  [ -f "$dl/$1" ] || curl -fL --retry 3 -o "$dl/$1" "$2"
  echo "$3  $dl/$1" | sha256sum -c --quiet - || { echo "checksum mismatch for $1" >&2; rm -f "$dl/$1"; exit 1; }
}
llvm=LLVM-20.1.8-Linux-X64
fetch $llvm.tar.xz https://github.com/llvm/llvm-project/releases/download/llvmorg-20.1.8/$llvm.tar.xz 1ead36b3dfcb774b57be530df42bec70ab2d239fbce9889447c7a29a4ddc1ae6
deb=http://deb.debian.org/debian/pool/main
fetch libc6.deb $deb/g/glibc/libc6_2.31-13+deb11u11_amd64.deb 05f7264da867b37f4c5ce49266b558ea1e81e05a9464f623152fca70f3550282
fetch libc6-dev.deb $deb/g/glibc/libc6-dev_2.31-13+deb11u11_amd64.deb e7f7b45d9c5cfcf37609f0b6efd3c645272c812144703af89dfd32218fcb0fd3
fetch linux-libc-dev.deb $deb/l/linux/linux-libc-dev_5.10.223-1_amd64.deb e3be603bd12377bb90fcfb0f2048ca3a81c886e49d6f09945b82159d1c54f7b2
rm -rf "$out"; mkdir -p "$out/lib" "$out/sysroot"
# Only what a C compile and a shared link need: the compiler driver, lld, the resource headers and the builtins library.
xz -dc "$dl/$llvm.tar.xz" | tar -x -C "$out" --strip-components=1 \
  "$llvm/bin/clang" "$llvm/bin/clang-20" "$llvm/bin/ld.lld" "$llvm/bin/lld" "$llvm/lib/clang/20/include" 2>/dev/null || true
xz -dc "$dl/$llvm.tar.xz" | tar -x -O "$llvm/lib/clang/20/lib/x86_64-unknown-linux-gnu/libclang_rt.builtins.a" > "$out/lib/libclang_rt.builtins-x86_64.a"
for d in libc6 libc6-dev linux-libc-dev; do
  tmp="$(mktemp -d)"; (cd "$tmp" && ar x "$dl/$d.deb" && tar -xf data.tar.* -C "$out/sysroot" 2>/dev/null); rm -rf "$tmp"
done
# The sysroot's linker scripts and symlinks point at absolute /lib paths: make them relative to the sysroot.
find "$out/sysroot" -name 'libc.so' -o -name 'libm.so' | while read -r f; do sed -i 's# /lib/x86_64-linux-gnu/# =/lib/x86_64-linux-gnu/#g; s# /usr/lib/x86_64-linux-gnu/# =/usr/lib/x86_64-linux-gnu/#g; s#AS_NEEDED ( /lib64/ld-linux-x86-64.so.2 )##' "$f"; done
# The official lld links libxml2 only for Windows manifest merging, which these builds never use, and libxml2.so.2 is not on every
# system. Ship a stub with the same versioned symbols (built here, with the packaging machine's own C compiler) and point lld's rpath at it.
stub="$(mktemp -d)"
nm -D --undefined-only "$out/bin/lld" | awk '/ U xml/ {print $2}' | sed 's/@/ /' | sort -u > "$stub/syms"
{ echo "void* xml_stub_unused(void) { return 0; }"; awk '{print "void* " $1 "(void) { return 0; }"}' "$stub/syms"; } > "$stub/xml.c"
{ awk '{v[$2]=v[$2] " " $1 ";"} END {for (k in v) print k " { global:" v[k] " };"}' "$stub/syms"; } > "$stub/xml.map"
# ld wants a version node to inherit from: chain 2.6.0 onto 2.4.30 as libxml2 does.
sed -i 's/^LIBXML2_2.6.0 \(.*\)$/LIBXML2_2.6.0 \1/' "$stub/xml.map"
${CC:-cc} -shared -fPIC -nostdlib -Wl,-soname,libxml2.so.2 -Wl,--version-script="$stub/xml.map" -o "$out/lib/libxml2.so.2" "$stub/xml.c"
patchelf --set-rpath '$ORIGIN/../lib' "$out/bin/lld"
rm -rf "$stub"
printf 'llvm 20.1.8 + debian 11 glibc 2.31-13+deb11u11 + linux-libc-dev 5.10.223-1\n' > "$out/TOOLCHAIN_VERSION"
du -sh "$out"
