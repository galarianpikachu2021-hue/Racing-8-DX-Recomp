#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Turn a Switch NSO or NRO (32- or 64-bit) into a section-less ELF that Ghidra's ELF loader can read with its dynamic symbols
and relocations (PT_LOAD segments + PT_DYNAMIC). Research aid for porting CTGP DX: nothing is written but the ELF.

    tools/ctgp/nx2elf.py <in.nso|in.nro> <out.elf> [--32|--64]

The width is taken from the dynamic section when not given (DT_NULL-terminated pairs that parse cleanly as 8-byte entries = 64-bit).
"""
import struct
import sys

import lz4.block


def load_nso(d):
    flags = struct.unpack_from("<I", d, 0xC)[0]
    segs = []
    for i, hoff in enumerate((0x10, 0x20, 0x30)):
        foff, moff, size = struct.unpack_from("<III", d, hoff)
        fsize = struct.unpack_from("<I", d, 0x60 + 4 * i)[0]
        raw = d[foff:foff + fsize]
        if flags & (1 << i):
            raw = lz4.block.decompress(raw, uncompressed_size=size)
        segs.append((moff, raw))
    bss = struct.unpack_from("<I", d, 0x3C)[0]
    return segs, bss


def load_nro(d):
    segs = []
    for hoff in (0x20, 0x28, 0x30):
        off, size = struct.unpack_from("<II", d, hoff)
        segs.append((off, d[off:off + size]))
    bss = struct.unpack_from("<I", d, 0x38)[0]
    return segs, bss


def main():
    src, dst = sys.argv[1], sys.argv[2]
    d = open(src, "rb").read()
    segs, bss = load_nso(d) if d[:4] == b"NSO0" else load_nro(d)
    end = max(o + len(r) for o, r in segs)
    img = bytearray(end)
    for o, r in segs:
        img[o:o + len(r)] = r
    mod0 = struct.unpack_from("<I", img, 4)[0]
    assert img[mod0:mod0 + 4] == b"MOD0", "no MOD0"
    dyn = mod0 + struct.unpack_from("<i", img, mod0 + 4)[0]
    if "--32" in sys.argv:
        wide = 4
    elif "--64" in sys.argv:
        wide = 8
    else:
        # 64-bit dynamic entries have zero high halves for tags; a 32-bit table read as 64-bit gives huge tags.
        tag64 = struct.unpack_from("<Q", img, dyn)[0]
        wide = 8 if tag64 < 0x80000000 else 4
    dyn_size = 0
    while True:
        tag = struct.unpack_from("<I" if wide == 4 else "<Q", img, dyn + dyn_size)[0]
        dyn_size += 2 * wide
        if tag == 0:
            break
    bss_end = end + bss
    is64 = wide == 8
    # Program headers: one PT_LOAD per segment (text RX, ro R, data+bss RW), PT_DYNAMIC.
    flagsets = [5, 4, 6]
    phdrs = []
    for i, (o, r) in enumerate(segs):
        memsz = len(r) + (bss if i == 2 else 0)
        phdrs.append((1, flagsets[i], o, len(r), memsz))
    phdrs.append((2, 6, dyn, dyn_size, dyn_size))
    if is64:
        ehsize, phentsize = 64, 56
    else:
        ehsize, phentsize = 52, 32
    data_off = 0x1000
    out = bytearray(data_off) + img
    e_ident = b"\x7fELF" + bytes([2 if is64 else 1, 1, 1, 0]) + b"\0" * 8
    machine = 183 if is64 else 40
    if is64:
        hdr = e_ident + struct.pack("<HHIQQQIHHHHHH", 3, machine, 1, 0, ehsize, 0, 0, ehsize, phentsize, len(phdrs), 0, 0, 0)
    else:
        hdr = e_ident + struct.pack("<HHIIIIIHHHHHH", 3, machine, 1, 0, ehsize, 0, 0x05000000, ehsize, phentsize, len(phdrs), 0, 0, 0)
    out[0:len(hdr)] = hdr
    pos = ehsize
    for typ, fl, vaddr, filesz, memsz in phdrs:
        if is64:
            ph = struct.pack("<IIQQQQQQ", typ, fl, data_off + vaddr, vaddr, vaddr, filesz, memsz, 0x1000 if typ == 1 else 8)
        else:
            ph = struct.pack("<IIIIIIII", typ, data_off + vaddr, vaddr, vaddr, filesz, memsz, fl, 0x1000 if typ == 1 else 4)
        out[pos:pos + phentsize] = ph
        pos += phentsize
    open(dst, "wb").write(out)
    print(f"{src}: {'64' if is64 else '32'}-bit, image {end:#x} + bss {bss:#x}, dynamic at {dyn:#x} ({dyn_size} bytes) -> {dst}")


if __name__ == "__main__":
    main()
