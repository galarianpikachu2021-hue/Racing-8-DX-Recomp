#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Generate the engine's CTGP DX site table and the exporter's extra block starts from tools/ctgp/sites_401.txt.

    tools/ctgp/gen_sites.py <main-4.0.1 image> <main-3.0.4 image> <spec> <out .inc> <out roots dir>

Spec lines (4.0.1 main offsets; '#' starts a comment):
    <site> <action> [args...] [| <3.0.3 site / note>]
Actions:
    base   wN       wN = base course index of the course index in wN       (value-transform)
    clamp  wN       wN = 0x10 when wN >= 0x7b                               (value-transform)
    set    xN V     xN = V, skip the instruction at <site>                  (instruction replacement)
    cmp    wN V     flags = compare wN with V, skip the instruction         (instruction replacement)
    branch T        continue at T (4.0.1 offset) instead of <site>          (instruction replacement)
    nop             skip the instruction at <site>
    root            no hook yet: only start blocks at <site> and the next instruction (so a later handler needs no rebuild)
    field  DST SRC F  DST = field F of the course whose index is SRC, for CTGP's course ids only (value-transform on DST).
                    SRC: wN (index in a register), xN+0xOFF (u32 index in memory), saved+0xOFF (u32 at the pointer kept by `save`).
                    F: name platform author cup flag base basebcp picture folder icon (strings: DST gets a guest pointer)
    save   xN       keep xN for later `saved+` sources (no other effect)
    call   NAME [R] [+N]  custom handler NAME in ctgp_dx.cpp with optional register R, hook at <site> (runs before the instruction
                    there); +N: the handler may continue at site + N (a block starts there)
For value-transforms the hook goes after the instruction at <site> when that instruction writes wN (a load of the value), else on
<site> itself. Every 4.0.1 offset is mapped to 3.0.4 with the masked instruction-window match of tools/recomp/port_hooks.py; a site
without a unique full match is an error. Output: CTGP_SITE(off304, off401, Act, reg, arg, arg2, "note") lines and ctgp_roots.txt
(hook address and resume address, "main <hex>") for data/mk8d-3.0.4 and data/mk8d-4.0.1.
"""
import os
import re
import sys

import capstone
import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "recomp"))
import port_hooks  # noqa: E402

A64 = capstone.Cs(capstone.CS_ARCH_ARM64, capstone.CS_MODE_ARM)
A64.detail = True


def insn(img, off):
    return next(A64.disasm(img[off:off + 4], off))


def writes(i, reg):
    """Does instruction i write register reg ('w8' / 'x8')?"""
    num = int(reg[1:])
    names = {f"w{num}", f"x{num}"}
    try:
        _, w = i.regs_access()
        return any(i.reg_name(r) in names for r in w)
    except capstone.CsError:
        return False


def main():
    img401, img304 = open(sys.argv[1], "rb").read(), open(sys.argv[2], "rb").read()
    old = port_hooks.np.frombuffer(img401[: len(img401) // 4 * 4], dtype="<u4").copy()
    new = port_hooks.np.frombuffer(img304[: len(img304) // 4 * 4], dtype="<u4").copy()

    def port(off):
        best, hits = port_hooks.find(old, new, off // 4, 24)
        if len(hits) == 1 and 16 <= best < 23:
            # weaker but unique: accept when the site instruction itself matches (address fields masked)
            k = int(hits[0])
            m = port_hooks.mask_of(np.array([old[off // 4], new[k]], dtype=np.uint32))
            if (old[off // 4] & m[0]) == (new[k] & m[1]):
                print(f"note: {off:#x} -> {k * 4:#x} matched {best}/24 (site instruction identical)", file=sys.stderr)
                return k * 4
        if len(hits) != 1 or best < 23:
            # anchor: the nearest instruction within 160 before/after the site that matches with a full, unique window; same
            # distance in 3.0.4, accepted when the site instruction matches there (masked)
            for d in sorted(range(-160, 161), key=abs):
                if d == 0:
                    continue
                a = off // 4 + d
                b2, h2 = port_hooks.find(old, new, a, 12)
                if len(h2) == 1 and b2 == 12:
                    k0 = int(h2[0]) - d
                    o = off // 4

                    def same(kk):  # the site instruction and the one after it match (masked)
                        for q in (0, 1):
                            m = port_hooks.mask_of(np.array([old[o + q], new[kk + q]], dtype=np.uint32))
                            if (old[o + q] & m[0]) != (new[kk + q] & m[1]):
                                return False
                        return True

                    for dk in sorted(range(-16, 17), key=abs):
                        if same(k0 + dk):
                            print(f"note: {off:#x} -> {(k0 + dk) * 4:#x} by anchor {a * 4:#x} (shift {dk})", file=sys.stderr)
                            return (k0 + dk) * 4
            # try a window that ends at the site instead (sites near the end of a function)
            best2, hits2 = port_hooks.find(old, new, max(0, off // 4 - 23), 24)
            if len(hits2) == 1 and best2 >= 23:
                return int(hits2[0]) * 4 + (off - (off // 4 - 23) * 4)
            raise SystemExit(f"site {off:#x}: no unique 3.0.4 match ({best}/24, {len(hits)} hits)")
        return int(hits[0]) * 4

    acts = {"base": "Base", "clamp": "Clamp", "set": "Set", "cmp": "Cmp", "branch": "Branch", "nop": "Nop", "call": "Call",
            "field": "Field", "save": "Save"}
    fields = ["name", "platform", "author", "cup", "flag", "base", "basebcp", "picture", "folder", "icon"]
    lines, roots401, roots304 = [], set(), set()
    calls = []
    for raw in open(sys.argv[3]):
        text = raw.split("#", 1)[0].strip()
        if not text:
            continue
        note = ""
        if "|" in text:
            text, note = (t.strip() for t in text.split("|", 1))
        p = text.split()
        # "A@B": 4.0.1 site A whose 3.0.4 equivalent B was checked by hand ("A@-": the site does not exist in 3.0.4)
        manual304 = None
        if "@" in p[0]:
            p[0], m304 = p[0].split("@")
            manual304 = -1 if m304 == "-" else int(m304, 16)
        site, act, args = int(p[0], 16), p[1], p[2:]
        reg, arg, arg2 = 0, 0, 0
        hook, resume = site, site + 4
        if act in ("base", "clamp"):
            reg = int(args[0][1:])
            if writes(insn(img401, site), args[0]):
                hook = site + 4
            resume = None
        elif act in ("set", "cmp"):
            reg, arg = int(args[0][1:]), int(args[1], 0)
        elif act == "root":
            roots401.update({site, site + 4})
            try:
                roots304.update({port(site), port(site + 4)})
            except SystemExit as e:  # a later handler will need its 3.0.4 address checked by hand
                print(f"warning: root {site:#x}: {e}", file=sys.stderr)
            continue
        elif act == "branch":
            resume = int(args[0], 16)
            arg = resume
        elif act == "field":
            reg = int(args[0][1:])
            src = args[1]
            if src.startswith("saved+"):
                arg = (2 << 8) | (int(src[6:], 16) << 16)
            elif "+" in src:
                r, off = src.split("+")
                arg = int(r[1:]) | (1 << 8) | (int(off, 16) << 16)
            else:
                arg = int(src[1:])
            arg2 = fields.index(args[2])
            if writes(insn(img401, site), args[0]):
                hook = site + 4
            resume = None
        elif act == "save":
            reg = int(args[0][1:])
            resume = None
        elif act == "call":
            calls.append(args[0])
            arg = len(calls) - 1
            resume = None
            for a in args[1:]:
                if a.startswith("+"):  # the handler continues N bytes on: that instruction must start a block too
                    resume = site + int(a[1:], 0)
                else:  # optional register argument for the handler (sN / wN / xN)
                    reg = int(a[1:])
        h304 = port(hook) if manual304 is None else (0 if manual304 < 0 else manual304 + (hook - site))
        if act == "branch":
            arg2 = port(resume)
        roots401.add(hook)
        if h304:
            roots304.add(h304)
        if resume is not None:
            roots401.add(resume)
            if h304:
                roots304.add((port(resume) if manual304 is None else h304 + 4) if act != "branch" else arg2)
        note = (note or f"{site:#x}").replace('"', "'")
        name = args[0] if act == "call" else ""
        lines.append(f'CTGP_SITE({h304:#x}, {hook:#x}, {acts[act]}, {reg}, {arg:#x}, {arg2:#x}, "{name}", "{note}")')
    with open(sys.argv[4], "w") as o:
        o.write("// SPDX-License-Identifier: GPL-3.0-or-later\n")
        o.write("// Generated by tools/ctgp/gen_sites.py from tools/ctgp/sites_401.txt (docs/CTGP_PORT.md). Do not edit by hand.\n")
        o.write("// CTGP_SITE(hook offset in 3.0.4 main, in 4.0.1 main, action, register, argument, 3.0.4 branch target, handler, note)\n")
        for l in lines:
            o.write(l + "\n")
    for ver, roots in (("3.0.4", roots304), ("4.0.1", roots401)):
        d = os.path.join(sys.argv[5], f"mk8d-{ver}")
        os.makedirs(d, exist_ok=True)
        with open(os.path.join(d, "ctgp_roots.txt"), "w") as o:
            o.write("# Extra block starts for the CTGP DX hooks (tools/ctgp/gen_sites.py). Format: <module> <hex offset>\n")
            for r in sorted(roots):
                o.write(f"main {r:x}\n")
    print(f"{len(lines)} sites, {len(roots401)} roots")


if __name__ == "__main__":
    main()
