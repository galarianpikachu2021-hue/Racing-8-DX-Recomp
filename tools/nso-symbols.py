#!/usr/bin/env python3
"""List the dynamic symbols of a decompressed-or-compressed NSO (via MOD0 -> .dynamic).

  python3 tools/nso-symbols.py <nso> [substring]

Reads the user's own local file; prints name, value (module-relative), size, defined/imported.
"""
import struct
import sys

def load_flat(path):
    raw = open(path, "rb").read()
    assert raw[:4] == b"NSO0", "not an NSO"
    flags = struct.unpack_from("<I", raw, 0xC)[0]
    segs = []
    for i in range(3):
        foff, vaddr, dsize = struct.unpack_from("<III", raw, 0x10 + i * 0x10)
        csize = struct.unpack_from("<I", raw, 0x60 + i * 4)[0]
        data = raw[foff:foff + (csize if flags & (1 << i) else dsize)]
        if flags & (1 << i):
            data = lz4(data, dsize)
        segs.append((vaddr, data))
    bss = struct.unpack_from("<I", raw, 0x3C)[0]
    end = max(v + len(d) for v, d in segs)
    img = bytearray(end + bss)
    for v, d in segs:
        img[v:v + len(d)] = d
    return bytes(img)

def lz4(src, out_size):
    dst = bytearray(); i = 0
    while i < len(src):
        tok = src[i]; i += 1
        lit = tok >> 4
        if lit == 15:
            while True:
                b = src[i]; i += 1; lit += b
                if b != 255: break
        dst += src[i:i + lit]; i += lit
        if i >= len(src): break
        off = src[i] | src[i + 1] << 8; i += 2
        ml = tok & 15
        if ml == 15:
            while True:
                b = src[i]; i += 1; ml += b
                if b != 255: break
        ml += 4
        for _ in range(ml):
            dst.append(dst[-off])
    assert len(dst) == out_size, (len(dst), out_size)
    return bytes(dst)

def symbols(img):
    mod0 = struct.unpack_from("<I", img, 4)[0]
    assert img[mod0:mod0 + 4] == b"MOD0"
    dyn = mod0 + struct.unpack_from("<i", img, mod0 + 4)[0]
    tags = {}
    o = dyn
    while True:
        tag, val = struct.unpack_from("<qQ", img, o); o += 16
        if tag == 0: break
        tags.setdefault(tag, val)
    symtab, strtab, hashtab = tags[6], tags[5], tags.get(4)
    count = struct.unpack_from("<I", img, hashtab + 4)[0] if hashtab else (strtab - symtab) // 24
    for i in range(count):
        name_off, info, other, shndx, value, size = struct.unpack_from("<IBBHQQ", img, symtab + i * 24)
        end = img.index(b"\0", strtab + name_off)
        yield img[strtab + name_off:end].decode(errors="replace"), value, size, shndx != 0

if __name__ == "__main__":
    img = load_flat(sys.argv[1])
    needle = sys.argv[2] if len(sys.argv) > 2 else ""
    for name, value, size, defined in symbols(img):
        if needle in name:
            print(f"{'def' if defined else 'imp'} {value:#010x} {size:#7x} {name}")
