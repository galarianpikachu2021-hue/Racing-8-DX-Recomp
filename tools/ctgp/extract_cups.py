#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Extracts CTGP DX tracks from the user's own pack zip as an ADDITIVE data mod: every track stays in its own CT_xx folder, nothing of the game's
own courses is overwritten.

    extract_cups.py <CTGPDX zip> <out mod dir> [CT_00 CT_01 ... | all]

Writes <out>/romfs/Course/CT_xx/**, Audio/Resource/Stream/pBGM_CT_xx_{N,F}.bfstm and UI/cmn/menu/ym_CoursePict_CT_xx_00^u.bntx, verbatim from the
pack. The pack's own message files, plugin and exefs are not copied. The game reaches the tracks through the extra-cup hook (see docs/CTGP.md).
"""
import re
import sys
import zipfile

PREFIX = "atmosphere/contents/0100152000022000/romfs/"
zip_path, out = sys.argv[1], sys.argv[2]
want = set(sys.argv[3:]) or {"all"}
with zipfile.ZipFile(zip_path) as z:
    names = z.namelist()
    tracks = sorted({m.group(1) for n in names if (m := re.match(re.escape(PREFIX) + r"Course/(CT_[0-9A-Fa-f]{2})/", n))})
    chosen = tracks if "all" in want else [t for t in tracks if t in want]
    count = 0
    for n in names:
        if n.endswith("/") or not n.startswith(PREFIX):
            continue
        rel = n[len(PREFIX):]
        m = re.search(r"CT_[0-9A-Fa-f]{2}", rel)
        if not m or m.group(0) not in chosen:
            continue
        if not (rel.startswith("Course/CT_") or "pBGM_CT_" in rel or "ym_CoursePict_CT_" in rel):
            continue
        target = f"{out}/romfs/{rel}"
        import os
        os.makedirs(os.path.dirname(target), exist_ok=True)
        with z.open(n) as src, open(target, "wb") as dst:
            while chunk := src.read(1 << 20):
                dst.write(chunk)
        count += 1
print(f"{len(chosen)} tracks, {count} files -> {out}")
