#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Splits generated recompiler units (recompiled_<mod>_<n>.c, ~20000 blocks each) into smaller files.

    split_units.py <unit.c> <out dir> <parts> [hot list]

One big unit peaks at 2-7.6 GB in the compiler and builds only one at a time; smaller pieces need less memory each, so
several build in parallel. The cut is at block-function boundaries: block functions already declare every block they
call, so each piece stands alone. The per-unit guard array (`static uint32_t recomp_gg_seen[N]`) becomes one global per unit
(defined in piece 0, extern in the others, same indices), other initialised globals are defined in piece 0 only, and the
block table goes in the last piece with declarations of every block. Output: <stem>_p<k>.c for k in 0..parts-1.

With a hot list (one block function name per line, e.g. from a coverage run or a profile), the unit's hot blocks go to
<stem>_hot.c (to be compiled with full optimisation) and only the rest is cut into the <parts> pieces (cheap optimisation).
"""
import os
import re
import sys

src, out_dir, parts = sys.argv[1], sys.argv[2], int(sys.argv[3])
hot = set(open(sys.argv[4]).read().split()) if len(sys.argv) > 4 else None
stem = os.path.basename(src).split(".")[0]
ident = re.sub(r"\W", "_", stem)
lines = open(src, encoding="utf-8").read().split("\n")
first = next(i for i, l in enumerate(lines) if l.startswith("void blk_"))
table = next(i for i, l in enumerate(lines) if l.startswith("const struct _recomp_ent _seg"))
head, body, tail = lines[:first], lines[first:table], lines[table:]
seen_re = re.compile(r"static uint32_t recomp_gg_seen\[(\d+)\];")
seen = [i for i, l in enumerate(head) if seen_re.match(l)]
if len(seen) != 1:
    sys.exit("unexpected header: no single recomp_gg_seen array")
count = seen_re.match(head[seen[0]]).group(1)
glob_name = f"recomp_gg_seen_{ident}"
starts = [i for i, l in enumerate(body) if l.startswith("void blk_")] + [len(body)]
funcs = [(re.match(r"void (blk_\w+)\(", body[starts[j]]).group(1), body[starts[j]:starts[j + 1]]) for j in range(len(starts) - 1)]
names = [n for n, _ in funcs]
hot_funcs = [f for f in funcs if hot is not None and f[0] in hot]
cold = [f for f in funcs if hot is None or f[0] not in hot]
per = max(1, -(-len(cold) // parts))
groups = [(f"{stem}_p{k}.c", cold[k * per:(k + 1) * per]) for k in range(parts)]
if hot is not None:
    groups.append((f"{stem}_hot.c", hot_funcs))
os.makedirs(out_dir, exist_ok=True)
for k, (name, group) in enumerate(groups):
    h = list(head)
    decl = f"uint32_t {glob_name}[{count}];" if k == 0 else f"extern uint32_t {glob_name}[{count}];"
    h[seen[0]] = decl + f"\n#define recomp_gg_seen {glob_name}"
    if k > 0:  # other initialised globals of the unit (unit 0 defines g_recomp_guard_host_v2) are defined once, in piece 0
        h = [re.sub(r"^(int|unsigned|uint32_t|uint64_t) (\w+)=[^;]*;$", r"extern \1 \2;", l) for l in h]
    text = h + [l for _, b in group for l in b]
    if k == len(groups) - 1:
        text += [f"void {n}(GuestContext*);" for n in names] + tail
    with open(os.path.join(out_dir, name), "w", encoding="utf-8") as f:
        f.write("\n".join(text))
print(f"{stem}: {len(hot_funcs)} hot, {len(cold)} cold")
