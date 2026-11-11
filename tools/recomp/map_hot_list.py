#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Carries a hot block list from one game version to another (e.g. 3.0.4 -> 4.0.1), so the new version's build gets the same tiering.

    map_hot_list.py <old nso dir> <new nso dir> <old hot list> <new hot list>

The NSO dirs are an export's `nso/` folder (the user's own files; nothing is written next to them). A module whose NSO is byte-identical
keeps its entries as they are. For a changed module (here only `main`), each hot block is looked up by its code: the instructions from the
block's start, with every PC-relative or data-layout immediate masked (branches, ADR/ADRP, literal loads, the low 12 bits that follow an
ADRP), must occur exactly once in the new module's text. Blocks that are not found uniquely are dropped (they are then compiled cold:
correct, only a little slower). Prints how many were carried over.
"""
import hashlib
import importlib.util
import os
import struct
import sys
from collections import defaultdict

WINDOW = 10  # instructions per fingerprint

here = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location("nsosym", os.path.join(here, "..", "nso-symbols.py"))
nsosym = importlib.util.module_from_spec(spec)
spec.loader.exec_module(nsosym)


def text_of(path):
    img = nsosym.load_flat(path)
    img = img[0] if isinstance(img, tuple) else img
    raw = open(path, "rb").read()
    toff, tsize = struct.unpack_from("<I", raw, 0x14)[0], struct.unpack_from("<I", raw, 0x18)[0]  # text vaddr, size
    return bytes(img[toff:toff + tsize]), toff


def mask(w):
    top6 = w >> 26
    if top6 in (0b000101, 0b100101):           # B, BL
        return w & 0xFC000000
    if (w >> 24) & 0xFE == 0x54 or (w >> 25) & 0x3F == 0x1A or (w >> 25) & 0x3F == 0x1B:  # B.cond, CBZ/CBNZ
        return w & 0xFF00001F
    if (w >> 25) & 0x3F == 0x1B or (w >> 24) & 0x7E == 0x36:  # TBZ/TBNZ
        return w & 0xFFF8001F
    if (w >> 24) & 0x1F == 0x10:               # ADR / ADRP
        return w & 0x9F00001F
    if (w >> 24) & 0x3B == 0x18:               # LDR (literal)
        return w & 0xFF00001F
    if (w >> 22) & 0x1FF in (0x122, 0x022):    # ADD immediate (64/32): the low 12 bits after an ADRP are a data offset
        return w & 0xFFC003FF
    if (w >> 22) & 0x3FF in (0x3E5, 0x2E5, 0x0E5, 0x1E5, 0x3F5, 0x2F5):  # LDR/STR unsigned offset (likely an ADRP pair)
        return w & 0xFFC003FF
    return w


def fingerprint(text, off):
    words = struct.unpack_from("<%dI" % WINDOW, text, off) if off + 4 * WINDOW <= len(text) else None
    if words is None:
        return None
    return hashlib.blake2b(struct.pack("<%dI" % WINDOW, *[mask(w) for w in words]), digest_size=12).digest()


def main():
    old_dir, new_dir, old_list, new_list = sys.argv[1:5]
    entries = [l.strip() for l in open(old_list) if l.strip()]
    by_module = defaultdict(list)
    for e in entries:
        _, mod, off = e.split("_", 2)
        by_module[mod].append((e, int(off, 16)))
    out = []
    for mod, items in by_module.items():
        old_p, new_p = os.path.join(old_dir, mod), os.path.join(new_dir, mod)
        if open(old_p, "rb").read() == open(new_p, "rb").read():
            out += [e for e, _ in items]
            print(f"{mod}: identical, {len(items)} kept")
            continue
        old_text, _ = text_of(old_p)
        new_text, _ = text_of(new_p)
        index = defaultdict(list)
        for off in range(0, len(new_text) - 4 * WINDOW, 4):
            fp = fingerprint(new_text, off)
            index[fp].append(off)
        # Pass 1: blocks whose fingerprint occurs exactly once. Their old->new shift is the local displacement of the code.
        items.sort(key=lambda t: t[1])
        unique = {}
        for e, off in items:
            hits = index.get(fingerprint(old_text, off), [])
            if len(hits) == 1:
                unique[off] = hits[0]
        anchors = sorted(unique.items())
        anchor_offs = [a for a, _ in anchors]
        import bisect
        found = by_shift = missing = 0
        for e, off in items:
            if off in unique:
                out.append(f"blk_{mod}_{unique[off]:016x}")
                found += 1
                continue
            # Pass 2: take the shift of the nearest uniquely matched block before it; accept when the code there has the same
            # masked fingerprint (ambiguous blocks) or the same first 4 masked instructions (code that changed a little further on).
            i = bisect.bisect_right(anchor_offs, off) - 1
            if i < 0:
                missing += 1
                continue
            cand = off + (anchors[i][1] - anchors[i][0])
            if 0 <= cand < len(new_text) - 4 * WINDOW and (
                    fingerprint(new_text, cand) == fingerprint(old_text, off) or
                    [mask(w) for w in struct.unpack_from("<4I", new_text, cand)] == [mask(w) for w in struct.unpack_from("<4I", old_text, off)]):
                out.append(f"blk_{mod}_{cand:016x}")
                by_shift += 1
            else:
                missing += 1
        print(f"{mod}: {found} unique, {by_shift} by local shift, {missing} dropped (of {len(items)})")
    with open(new_list, "w") as f:
        f.write("\n".join(sorted(set(out))) + "\n")
    print(f"wrote {len(set(out))} entries to {new_list}")


if __name__ == "__main__":
    main()
