#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Map CTGP DX patch sites from 3.0.3 (32-bit) to a 64-bit build (3.0.4 / 4.0.1), instruction by instruction.

    tools/ctgp/map_sites.py <hooksites.txt> <main-3.0.3.bin> <f303.tsv> <main-new.bin> <fnew.tsv> <matches.tsv> [--json out]

For every site the containing 3.0.3 function is mapped through match_funcs.py's output. Inside the matched 64-bit function the site
instruction is found by its role:
  * load/store with an immediate offset (`ldr r0,[r0,#0x1a0]`): the same offset with the same access size, same ordinal among those
  * compare with an immediate (`cmp r0,#0x7b`): the same immediate, same ordinal
  * function entry (offset 0..prologue): the 64-bit function's entry
  * floating-point divide (`vdiv`): `fdiv` with the same ordinal
Everything else is reported as "manual" with the function pair. The ordinal is only trusted when both functions contain the same
number of candidates. Output: one line per site, `3.0.3 site -> new site (method, confidence)`.
"""
import bisect
import json
import re
import struct
import sys

import capstone

A32 = capstone.Cs(capstone.CS_ARCH_ARM, capstone.CS_MODE_ARM)
A64 = capstone.Cs(capstone.CS_ARCH_ARM64, capstone.CS_MODE_ARM)


def load_funcs(path):
    starts = []
    for line in open(path, encoding="utf-8", errors="replace", newline="\n"):
        p = line.split("\t", 2)
        try:
            starts.append(int(p[0], 16))
        except ValueError:
            pass
    return sorted(set(starts))


def func_range(starts, f, img_len):
    i = bisect.bisect_right(starts, f)
    end = starts[i] if i < len(starts) else img_len
    return f, min(end, f + 0x20000)


def insns(md, img, a, b):
    return list(md.disasm(img[a:b], a))


IMM_RE = re.compile(r"#(-?0x[0-9a-f]+|-?\d+)")
MEM_RE = re.compile(r"\[(\w+)(?:, #(-?0x[0-9a-f]+|-?\d+))?\]")


def mem_off(i):
    m = MEM_RE.search(i.op_str)
    if not m:
        return None
    return int(m.group(2), 0) if m.group(2) else 0


def size_class(mn):
    mn = mn.lower()
    if mn.startswith(("ldrb", "strb", "ldrsb")):
        return 1
    if mn.startswith(("ldrh", "strh", "ldrsh")):
        return 2
    return 4


def kind_of32(i):
    mn = i.mnemonic.lower()
    base = re.sub(r"(eq|ne|hs|lo|mi|pl|vs|vc|hi|ls|ge|lt|gt|le|al)$", "", mn) if len(mn) > 3 else mn
    if base.startswith(("ldr", "str")) and "[" in i.op_str and "pc" not in i.op_str and mem_off(i) not in (None, 0):
        return ("mem", "ld" if base.startswith("ldr") else "st", mem_off(i), size_class(base))
    if base in ("cmp", "cmn") and "#" in i.op_str:
        v = int(IMM_RE.search(i.op_str).group(1), 0)
        return ("cmp", -v if base == "cmn" else v)
    if base.startswith("vdiv"):
        return ("fdiv",)
    if base.startswith("vmul"):
        return ("fmul",)
    if base == "tst" and "#" in i.op_str:
        return ("tst", int(IMM_RE.search(i.op_str).group(1), 0))
    if base in ("mov", "movw") and "#" in i.op_str and i.op_str.count(",") == 1:
        return ("movimm", int(IMM_RE.search(i.op_str).group(1), 0))
    return None


def matches64(i, k):
    mn = i.mnemonic.lower()
    if k[0] == "mem":
        if not (mn.startswith("ldr") if k[1] == "ld" else mn.startswith("str")) or "[" not in i.op_str:
            return False
        off = mem_off(i)
        if off != k[2]:
            return False
        sz = 1 if mn.endswith("b") or mn in ("ldrsb",) else 2 if mn.endswith("h") or mn == "ldrsh" else 4
        return sz == k[3] or (k[3] == 4 and sz == 4)
    if k[0] == "cmp":
        if mn not in ("cmp", "cmn") or "#" not in i.op_str:
            return False
        v = int(IMM_RE.search(i.op_str).group(1), 0)
        return (v if mn == "cmp" else -v) == k[1]
    if k[0] == "fdiv":
        return mn == "fdiv"
    if k[0] == "fmul":
        return mn == "fmul"
    if k[0] == "tst":
        return mn == "tst" and "#" in i.op_str and int(IMM_RE.search(i.op_str).group(1), 0) == k[1]
    if k[0] == "movimm":
        return mn in ("mov", "movz", "orr") and "#" in i.op_str and int(IMM_RE.search(i.op_str).group(1), 0) == k[1]
    return False


def main():
    sites_p, old_bin, old_f, new_bin, new_f, matches_p = sys.argv[1:7]
    js = sys.argv[sys.argv.index("--json") + 1] if "--json" in sys.argv else None
    old, new = open(old_bin, "rb").read(), open(new_bin, "rb").read()
    so, sn = load_funcs(old_f), load_funcs(new_f)
    fmap = {}
    for line in open(matches_p):
        a, b, how = line.rstrip("\n").split("\t")
        fmap[int(a, 16)] = (int(b, 16), how)
    out = []
    seen = set()
    for line in open(sites_p):
        p = line.split()
        if len(p) < 4 or p[1].startswith("thunk_"):
            continue
        kind, args = p[2], p[3:]
        if "?" in args[:1]:
            continue
        fn = int(args[0], 16)
        if kind in ("INLINE",):
            off = int(args[2], 16) if len(args) > 2 and args[2] != "?" else 0
        elif kind in ("INLINE0", "FUNC", "W32"):
            off = 0
        else:  # W32AT
            off = int(args[1], 16) if args[1] != "?" else 0
        site = fn + off
        if kind == "W32":
            # absolute address: find the containing function
            i = bisect.bisect_right(so, site) - 1
            fn, off = so[i], site - so[i]
        key = (kind, site)
        if key in seen:
            continue
        seen.add(key)
        rec = {"group": p[1], "kind": kind, "old_fn": fn, "old_site": site, "handler": args[1] if kind != "W32" else None}
        f_old = max((f for f in so if f <= fn), default=None) if fn not in so else fn
        if f_old not in fmap:
            rec.update(method="unmatched-function")
            out.append(rec)
            continue
        f_new, how = fmap[f_old]
        rec.update(new_fn=f_new, fn_match=how)
        if site == f_old or (kind == "INLINE0" and off == 0 and site == f_old) or kind == "FUNC":
            rec.update(new_site=f_new, method="entry", confidence="high" if site == f_old else "check")
            out.append(rec)
            continue
        a0, a1 = func_range(so, f_old, len(old))
        b0, b1 = func_range(sn, f_new, len(new))
        oi = insns(A32, old, a0, a1)
        ni = insns(A64, new, b0, b1)
        target = next((i for i in oi if i.address == site), None)
        if target is None:
            rec.update(method="no-insn")
            out.append(rec)
            continue
        k = kind_of32(target)
        if k is None:
            rec.update(method="manual", old_insn=f"{target.mnemonic} {target.op_str}")
            out.append(rec)
            continue
        oc = [i for i in oi if kind_of32(i) == k]
        nc = [i for i in ni if matches64(i, k)]
        ordn = oc.index(target)
        rec.update(old_insn=f"{target.mnemonic} {target.op_str}", n_old=len(oc), n_new=len(nc), ordinal=ordn)
        if len(nc) == len(oc) and nc:
            rec.update(new_site=nc[ordn].address, new_insn=f"{nc[ordn].mnemonic} {nc[ordn].op_str}", method=k[0],
                       confidence="high" if len(oc) <= 3 else "medium")
        elif nc:
            rec.update(method=k[0] + "-count-mismatch", candidates=[i.address for i in nc[:8]])
        else:
            rec.update(method=k[0] + "-none")
        out.append(rec)
    for r in out:
        ns = f"{r['new_site']:#x}" if "new_site" in r else "-"
        print(f"{r['group']:16s} {r['kind']:7s} {r['old_site']:#9x} -> {ns:>9s}  {r['method']:24s} {r.get('confidence', ''):6s} "
              f"{r.get('old_insn', '')}  =>  {r.get('new_insn', '')}  {('cands ' + ','.join(hex(c) for c in r['candidates'])) if 'candidates' in r else ''}"
              f"{' fn ' + hex(r['new_fn']) if 'new_fn' in r and 'new_site' not in r else ''}")
    if js:
        json.dump(out, open(js, "w"), indent=1)
    ok = sum(1 for r in out if "new_site" in r)
    print(f"# {ok}/{len(out)} sites mapped", file=sys.stderr)


if __name__ == "__main__":
    main()
