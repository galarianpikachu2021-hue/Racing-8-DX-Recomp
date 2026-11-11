#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""ui-baseline-hashes.py <dumped UI/cmn dir> <out file>: SHA-1 of every file inside the game's menu archives (UI/cmn/*.sarc, and
inside every Yaz0-compressed SARC member, recursively), one "archive/member[/inner...]<TAB>sha1" per line. Only hashes are written:
the launcher's menu merge (app/src/menu_merge.cpp) uses them to tell which archive members a mod changed relative to the update it
was made for, so mods made for 3.0.x menus work on newer updates. Nothing of the game itself is stored."""
import hashlib, os, struct, sys

def yaz0_decode(d):
    size = struct.unpack_from(">I", d, 4)[0]
    out = bytearray(); src = 16
    while len(out) < size and src < len(d):
        flags = d[src]; src += 1
        for bit in range(7, -1, -1):
            if len(out) >= size: break
            if flags >> bit & 1:
                out.append(d[src]); src += 1
            else:
                b1, b2 = d[src], d[src + 1]; src += 2
                dist = ((b1 & 0x0F) << 8 | b2) + 1
                n = b1 >> 4
                if n == 0: n = d[src] + 0x12; src += 1
                else: n += 2
                for _ in range(n): out.append(out[-dist])
    return bytes(out)

def sarc_members(d):
    if d[:4] != b"SARC": return None
    data_off = struct.unpack_from("<I", d, 0x0C)[0]; count = struct.unpack_from("<H", d, 0x1A)[0]
    names = 0x20 + count * 16 + 8
    out = []
    for i in range(count):
        h, attr, s, e = struct.unpack_from("<IIII", d, 0x20 + i * 16)
        # Members without a stored name (the layout archives inside .szs files) are known by their name hash.
        n = d[names + (attr & 0xFFFFFF) * 4:].split(b"\0", 1)[0].decode() if attr >> 24 == 1 else "#%08x" % h
        out.append((n, d[data_off + s:data_off + e]))
    return out

BLOCK = 4096

def walk(prefix, d, lines):
    line = f"{prefix}\t{hashlib.sha1(d).hexdigest()}"
    if d[:4] == b"BNTX" and len(d) > 4 * BLOCK:
        # Texture archives: also 8-byte hash prefixes of every 4 KiB block, so a mod's changed textures can be told apart from
        # the update's own changes inside the same file (block-wise merge).
        line += "\t" + "".join(hashlib.sha1(d[i:i + BLOCK]).hexdigest()[:16] for i in range(0, len(d), BLOCK))
    lines.append(line)
    inner = yaz0_decode(d) if d[:4] == b"Yaz0" else d
    members = sarc_members(inner)
    if members is None: return
    for n, b in members: walk(f"{prefix}/{n}", b, lines)

src, out = sys.argv[1], sys.argv[2]
lines = []
for f in sorted(os.listdir(src)):
    if f.endswith(".sarc"): walk(f, open(os.path.join(src, f), "rb").read(), lines)
open(out, "w").write("\n".join(lines) + "\n")
print(f"{len(lines)} hashes -> {out}")
