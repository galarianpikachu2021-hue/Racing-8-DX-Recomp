#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Caches guest registers in C locals inside each recompiled block.

    tools/recomp-regcache.py <exefs dir> [--module main] [--unit 29] [--check] [--revert-from <orig dir>]

The exporter writes every guest register access as `c->x[N]`: a load or store through
the GuestContext pointer. The compiler can't keep those in CPU registers, because `c`
escapes into the memory helpers (recomp_load64(c, ...) and friends), so each use goes
through memory. This rewrites each block function so that

    uint64_t _x1=c->x[1], _x8=c->x[8];          // once, after the code guard
    ... every c->x[N] becomes _xN ...
    { c->x[8]=_x8; return; }                    // written registers, at every exit

Only registers the function writes are stored back (a read-only register is just loaded).
Stores happen at every `return`, before the chained tail call, before recomp_unhandled,
and at the end of the function, so the context is exactly what the dispatcher, the host
hooks and the next block expect whenever control leaves the function.

Conservative by design: a function is left unchanged if it
  - takes the address of a register (`&c->x[N]`),
  - hands `c` to anything other than the memory helpers (recomp_load*/store*/ldp*/stp*),
    RECOMP_FPX_OPEN, recomp_unhandled and the chained block call (so recomp_svc, ldxr/stxr,
    clrex, cntpct, fp_estimate and friends keep working on a context that is always in
    sync), or
  - has no `c->x[N]` at all.
It is idempotent, and `--check` only reports counts.
"""
import pathlib
import re
import sys

FUNC = re.compile(r"^void (blk_\w+)\(GuestContext\* c\)\{\n(.*?)^\}\n", re.S | re.M)
REG = re.compile(r"c->x\[(\d+)\]")
WRITE_AFTER = re.compile(r"\s*(?:=(?!=)|\+=|-=|\*=|/=|%=|\|=|&=|\^=|<<=|>>=|\+\+|--)")
GUARD = re.compile(r"^    RECOMP_GG_GUARD\([^\n]*\n", re.M)
CHAIN = ("if (c->halted || c->pending_svc != ~UINT64_C(0) || --c->chain_budget <= 0) return; "
         "RECOMP_TAIL return ")
SAFE_C = re.compile(
    r"recomp_(?:load|store)(?:8|16|32|64)\(c,|recomp_(?:ldp|stp)(?:32|64)\(c,|RECOMP_FPX_OPEN\(c\)"
    r"|recomp_unhandled\(c,|return blk_\w+\(c\);|c->\w+")
MARK = "/*RC*/"


def transform(body):
    """Returns the new body, or None if the function is left alone."""
    if MARK in body:
        return None
    g = GUARD.search(body)
    if not g or "&c->x[" in body:
        return None
    # Every other use of the identifier `c` must be one of the known-safe forms.
    rest = SAFE_C.sub("", body)
    if re.search(r"(?<![\w>.])c(?![\w])", rest):
        return None
    used, written = set(), set()
    for m in REG.finditer(body):
        n = int(m.group(1))
        used.add(n)
        tail = body[m.end():m.end() + 6]
        head = body[max(0, m.start() - 2):m.start()]
        if WRITE_AFTER.match(tail) or head in ("++", "--"):
            written.add(n)
    if not used:
        return None
    stores = "".join(f"c->x[{n}]=_x{n}; " for n in sorted(written))
    out = REG.sub(lambda m: f"_x{m.group(1)}", body)
    # The chained tail call: stores go first, and its own `return;` needs none.
    out = out.replace(CHAIN, stores + CHAIN.replace("return;", "\x00RET;"))
    out = re.sub(r"recomp_unhandled\(c,([^;]*);", lambda m: "{ " + stores + "recomp_unhandled(c," + m.group(1) + "; }", out)
    out = re.sub(r"\breturn;", lambda m: "{ " + stores + "return; }" if stores else "return;", out)
    out = out.replace("\x00RET;", "return;")
    # Prologue after the code guard; the stores again at the very end for a fall-off.
    gi = g.end()
    loads = ", ".join(f"_x{n}=c->x[{n}]" for n in sorted(used))
    out = out[:gi] + f"    {MARK} uint64_t {loads};\n" + out[gi:]
    if stores:
        out = out.rstrip("\n") + f"\n    {stores}\n"
    return out


def process(path, check):
    text = path.read_text(encoding="latin-1")
    done = skipped = 0
    parts, last = [], 0
    for m in FUNC.finditer(text):
        new = transform(m.group(2))
        parts.append(text[last:m.start()])
        if new is None:
            parts.append(m.group(0))
            skipped += 1
        else:
            parts.append(f"void {m.group(1)}(GuestContext* c){{\n{new}}}\n")
            done += 1
        last = m.end()
    parts.append(text[last:])
    if not check and done:
        path.write_text("".join(parts), encoding="latin-1")
    return done, skipped


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    opts = dict(a[2:].split("=", 1) for a in sys.argv[1:] if a.startswith("--") and "=" in a)
    check = "--check" in sys.argv
    root = pathlib.Path(args[0])
    for mod_dir in sorted(p for p in root.iterdir() if (p / "recomp_runtime.h").exists()):
        if "module" in opts and mod_dir.name != opts["module"]:
            continue
        done = skipped = 0
        for src in sorted((mod_dir / "src").glob("recompiled_*_*.c")):
            if "unit" in opts and not src.name.endswith(f"_{opts['unit']}.c"):
                continue
            d, s = process(src, check)
            done += d
            skipped += s
        print(f"{mod_dir.name}: {done} blocks cached, {skipped} left alone")


if __name__ == "__main__":
    main()
