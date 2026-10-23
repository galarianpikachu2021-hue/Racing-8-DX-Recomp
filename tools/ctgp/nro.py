# SPDX-License-Identifier: GPL-3.0-or-later
"""Reads a Switch NRO (32- or 64-bit) into a flat image and lists its dynamic symbols and relocations."""
import struct, sys


class Nro:
    def __init__(self, path):
        d = open(path, "rb").read()
        self.raw = d
        assert d[0x10:0x14] == b"NRO0"
        self.text = struct.unpack_from("<II", d, 0x20)
        self.rodata = struct.unpack_from("<II", d, 0x28)
        self.data = struct.unpack_from("<II", d, 0x30)
        self.bss = struct.unpack_from("<I", d, 0x38)[0]
        end = max(o + s for o, s in (self.text, self.rodata, self.data))
        self.image = bytearray(d[:end] + b"\0" * self.bss)
        mod_off = struct.unpack_from("<I", d, 4)[0]
        assert d[mod_off:mod_off + 4] == b"MOD0", "no MOD0"
        dyn_rel = struct.unpack_from("<i", d, mod_off + 4)[0]
        self.dyn_off = mod_off + dyn_rel
        self.is32 = None

    def dynamic(self, wide=4):
        tags = {}
        pos = self.dyn_off
        while True:
            tag, val = struct.unpack_from("<II" if wide == 4 else "<QQ", self.raw, pos)
            if tag == 0:
                break
            tags.setdefault(tag, []).append(val)
            pos += wide * 2
        return tags


if __name__ == "__main__":
    n = Nro(sys.argv[1])
    print("text", [hex(x) for x in n.text], "rodata", [hex(x) for x in n.rodata], "data", [hex(x) for x in n.data], "bss", hex(n.bss))
    t = n.dynamic(4)
    print({hex(k): [hex(x) for x in v] for k, v in t.items()})
