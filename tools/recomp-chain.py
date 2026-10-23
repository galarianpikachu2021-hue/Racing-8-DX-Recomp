#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Turns a recompiled image's direct branches into direct block calls.

    tools/recomp-chain.py <exefs dir> [--check]

The exporter ends every block with `c->pc=<target>; return;`, so each edge goes
back to the module's dispatcher loop (recomp_image_run_slice), which looks the
next block up in a 24 MB index and calls it indirectly. Profiling MK8D put that
round trip at about 17% of the guest cores. When the target of an unconditional
B or BL (or a block's fall-through) is itself the start of a block, this rewrites
the exit into a guaranteed tail call of that block:

    { void blk_main_X(GuestContext*); c->pc=g_module_base+0x...ULL;
      if (c->halted || c->pending_svc != ~UINT64_C(0) || --c->chain_budget <= 0) return;
      RECOMP_TAIL return blk_main_X(c); }

These are the dispatcher's own checks, in the dispatcher's order, so the budget
(interrupt latency, block accounting), SVCs and halts behave as before. Targets
in another module or mid-block are left alone. Host hooks are unaffected: they
only ever ran on returns to the host, and a chain never leaves its module.

RECOMP_TAIL is `__attribute__((musttail))` on GCC 15+ and Clang, so the stack
never grows. With any other compiler it's empty and the budget bounds the depth.
The rewrite is idempotent, and the generator (suyu fork, arm64_to_c.h ChainTo)
emits the same text, so new exports don't need this.
"""
import pathlib
import re
import sys

ENTRY = re.compile(rb"\{0x([0-9a-f]+)ULL, 0x[0-9a-f]+ULL, (blk_\w+)\}")
EXIT = re.compile(rb"c->pc=g_module_base\+0x([0-9a-f]+)ULL; return;")
TAIL_DEF = b"""
/* kart-recomp block chaining: a direct branch to a block start calls it in tail
   position. musttail makes that a jump, so the stack never grows; without it the
   chain budget bounds the depth. */
#if (defined(__GNUC__) && !defined(__clang__) && __GNUC__ >= 15) || \\
    (defined(__clang__) && defined(__has_attribute))
#if defined(__has_attribute)
#if __has_attribute(musttail)
#define RECOMP_TAIL __attribute__((musttail))
#endif
#endif
#endif
#ifndef RECOMP_TAIL
#define RECOMP_TAIL
#endif
"""
ANCHOR = b"#define RECOMP_LIKELY(x) __builtin_expect(!!(x), 1)\n"


def chain(target_hex, name):
    return (b"{ void " + name + b"(GuestContext*); c->pc=g_module_base+0x" + target_hex +
            b"ULL; if (c->halted || c->pending_svc != ~UINT64_C(0) || --c->chain_budget <= 0) return; "
            b"RECOMP_TAIL return " + name + b"(c); }")


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    check = "--check" in sys.argv
    root = pathlib.Path(args[0])
    for mod_dir in sorted(p for p in root.iterdir() if (p / "recomp_runtime.h").exists()):
        header = mod_dir / "recomp_runtime.h"
        h = header.read_bytes()
        if b"RECOMP_TAIL" not in h:
            # After the RECOMP_LIKELY block's closing #endif.
            at = h.index(ANCHOR)
            at = h.index(b"#endif\n", at) + len(b"#endif\n")
            h = h[:at] + TAIL_DEF + h[at:]
            if not check:
                header.write_bytes(h)
        sources = sorted((mod_dir / "src").glob("*.c"))
        starts = {}
        for src in sources:
            for m in ENTRY.finditer(src.read_bytes()):
                starts[int(m.group(1), 16)] = m.group(2)
        total = changed = 0
        for src in sources:
            text = src.read_bytes()
            count = [0, 0]

            def sub(m):
                count[0] += 1
                name = starts.get(int(m.group(1), 16))
                if name is None:
                    return m.group(0)
                count[1] += 1
                return chain(m.group(1), name)

            new = EXIT.sub(sub, text)
            total += count[0]
            changed += count[1]
            if new != text and not check:
                src.write_bytes(new)
        print(f"{mod_dir.name}: {len(starts)} blocks, {changed} of {total} direct exits chained")


if __name__ == "__main__":
    main()
