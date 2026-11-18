#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""CTGP DX port aid: for each hook/patch site found in CTGPDX.nro (HookSites.java output), show the 3.0.3 game instructions at the
site and the plugin's handler stub (disassembled to its return), with the plugin C functions the stub calls.

    tools/ctgp/sites.py <hooksites.txt> <CTGPDX.nro> <3.0.3 main NSO> [ghidra base of the nro, default 0x10000]
"""
import struct
import sys

import capstone

sys.path.insert(0, __import__("os").path.dirname(__file__))


def nso_image(path):
    import lz4.block
    d = open(path, "rb").read()
    if d[:4] != b"NSO0":
        return d
    flags = struct.unpack_from("<I", d, 0xC)[0]
    segs = []
    for i, h in enumerate((0x10, 0x20, 0x30)):
        fo, mo, sz = struct.unpack_from("<III", d, h)
        fs = struct.unpack_from("<I", d, 0x60 + 4 * i)[0]
        raw = d[fo:fo + fs]
        if flags & (1 << i):
            raw = lz4.block.decompress(raw, uncompressed_size=sz)
        segs.append((mo, raw))
    img = bytearray(max(o + len(r) for o, r in segs))
    for o, r in segs:
        img[o:o + len(r)] = r
    return bytes(img)


def main():
    sites, nro, game = sys.argv[1], open(sys.argv[2], "rb").read(), nso_image(sys.argv[3])
    gbase = int(sys.argv[4], 16) if len(sys.argv) > 4 else 0x10000
    md = capstone.Cs(capstone.CS_ARCH_ARM, capstone.CS_MODE_ARM)
    seen = set()
    for line in open(sites):
        p = line.split()
        if len(p) < 4 or p[1].startswith("thunk_"):
            continue
        kind, args = p[2], p[3:]
        key = (kind, tuple(args))
        if key in seen:
            continue
        seen.add(key)
        h = lambda s: int(s, 16) if s != "?" else None
        if kind in ("INLINE", "INLINE0", "FUNC"):
            fn = h(args[0])
            handler = h(args[1]) if len(args) > 1 else None
            off = h(args[2]) if kind == "INLINE" and len(args) > 2 else 0
            if off is None:
                off = 0
            site = fn + off if fn is not None else None
            print(f"\n## {p[1]} {kind} game 3.0.3 fn {fn:#x} + {off:#x} = {site:#x}" if site is not None else f"\n## {kind} ?")
            if site is not None:
                for i in md.disasm(game[site - 8:site + 12], site - 8):
                    print(f"    {'>' if i.address == site else ' '} {i.address:#x} {i.mnemonic} {i.op_str}")
            if handler:
                a = handler - gbase
                print(f"  handler nro {a:#x}:")
                n = 0
                for i in md.disasm(nro[a:a + 0x200], a):
                    print(f"      {i.address:#x} {i.mnemonic} {i.op_str}" + (f"   -> C fn (ghidra {int(i.op_str[1:], 16) + gbase:#x})" if i.mnemonic == "bl" else ""))
                    n += 1
                    if (i.mnemonic.startswith("pop") and "pc" in i.op_str) or i.mnemonic in ("bx",) or n > 40:
                        break
        else:
            fn = h(args[0])
            if kind == "W32":
                site, val = fn, h(args[1]) if len(args) > 1 else None
            else:
                site = fn + h(args[1]) if h(args[1]) is not None else None
                val = h(args[2]) if len(args) > 2 else None
            print(f"\n## {p[1]} {kind} game 3.0.3 {site:#x} := {val:#x}" if val is not None and site is not None else f"\n## {p[1]} {kind} {' '.join(args)}")
            if site is not None:
                for i in md.disasm(game[site - 4:site + 8], site - 4):
                    print(f"    {'>' if i.address == site else ' '} {i.address:#x} {i.mnemonic} {i.op_str}")
                if val is not None:
                    for i in md.disasm(struct.pack("<I", val), site):
                        print(f"      new: {i.mnemonic} {i.op_str}")


if __name__ == "__main__":
    main()
