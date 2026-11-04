#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# test_module_guard.sh: the engine refuses to start without a built game (exit 5) and refuses modules built for another engine
# version or ABI (exit 4), with the reason on stderr. Needs only the engine binary and a C compiler for the stub modules.
# KARTRECOMP_ENGINE_BIN: the engine (default: the research workspace's build tree).
set -uo pipefail
bin="${KARTRECOMP_ENGINE_BIN:-/home/carter/reverse-engineering/mk8d/mk8-recomp/build/suyu-nojit-clang/bin/mk8d-engine}"
[ -x "$bin" ] || { echo "FAIL: no engine at $bin"; exit 1; }
tmp="$(mktemp -d)"; trap 'rm -rf "$tmp"' EXIT
expect() {  # <exit code> <message fragment> <description> -- <command...>
  local code="$1" frag="$2" what="$3"; shift 4
  local out; out=$("$@" 2>&1 </dev/null); local rc=$?
  [ "$rc" -eq "$code" ] && echo "$out" | grep -q "$frag" || { echo "FAIL: $what (exit $rc, wanted $code with '$frag'): $out" | head -c 600; exit 1; }
}
expect 5 "No game has been built yet" "no module folder" -- "$bin"
mkdir -p "$tmp/empty"
expect 5 "No game has been built yet" "empty module folder" -- "$bin" --module-dir "$tmp/empty"
mkdir -p "$tmp/badabi"
cat > "$tmp/stub.c" <<'EOT'
unsigned recomp_image_abi(void) { return 99; }
void* recomp_image_lookup(unsigned long pc) { (void)pc; return 0; }
void recomp_image_set_base(unsigned long b) { (void)b; }
EOT
clang -shared -fPIC -o "$tmp/badabi/recompiled_image.so" "$tmp/stub.c" || { echo "FAIL: cannot build the stub module"; exit 1; }
expect 4 "ABI 99" "module with an unsupported ABI" -- "$bin" --module-dir "$tmp/badabi"
mkdir -p "$tmp/otherengine"; cp "$tmp/badabi/recompiled_image.so" "$tmp/otherengine/"
echo '{ "engineVersion": "some-other-engine" }' > "$tmp/otherengine/module.json"
expect 4 "some-other-engine" "modules built for another engine version" -- "$bin" --module-dir "$tmp/otherengine"
echo PASS
