# SPDX-License-Identifier: GPL-3.0-or-later
"""Minimal SARC archive reader/writer (little endian) for the game's message and UI archives."""
import struct


def _hash(name: str, mult: int = 0x65) -> int:
    h = 0
    for c in name.encode():
        h = (h * mult + c) & 0xFFFFFFFF
    return h


class Sarc:
    def __init__(self, data: bytes | None = None):
        self.files: dict[str, bytes] = {}
        self.order: list[str] = []
        self.alignment = 0x1000
        if data is not None:
            self._read(data)

    def _read(self, data: bytes):
        assert data[:4] == b"SARC", "not a SARC"
        hdr, bom, size, data_off = struct.unpack_from("<HHII", data, 4)
        assert bom == 0xFEFF
        assert data[0x14:0x18] == b"SFAT"
        _, count, mult = struct.unpack_from("<HHI", data, 0x18)
        nodes = []
        pos = 0x20
        for _ in range(count):
            h, attr, start, end = struct.unpack_from("<IIII", data, pos)
            nodes.append((h, attr, start, end))
            pos += 16
        assert data[pos:pos + 4] == b"SFNT"
        names_off = pos + 8
        for h, attr, start, end in nodes:
            if attr >> 24 == 1:
                noff = names_off + (attr & 0xFFFFFF) * 4
                nend = data.index(b"\0", noff)
                name = data[noff:nend].decode()
            else:
                name = f"{h:08x}"
            self.files[name] = data[data_off + start:data_off + end]
            self.order.append(name)

    def write(self) -> bytes:
        names = sorted(self.files, key=lambda n: _hash(n))
        sfnt = bytearray()
        offsets = {}
        for n in names:
            offsets[n] = len(sfnt) // 4
            sfnt += n.encode() + b"\0"
            while len(sfnt) % 4:
                sfnt += b"\0"
        nodes = bytearray()
        blob = bytearray()
        for n in names:
            while len(blob) % 128:  # the game's message archives align every member to 0x80
                blob += b"\0"
            start = len(blob)
            blob += self.files[n]
            nodes += struct.pack("<IIII", _hash(n), 0x01000000 | offsets[n], start, len(blob))
        sfat = b"SFAT" + struct.pack("<HHI", 0xC, len(names), 0x65) + bytes(nodes)
        sfnt_blk = b"SFNT" + struct.pack("<HH", 8, 0) + bytes(sfnt)
        header_len = 0x14 + len(sfat) + len(sfnt_blk)
        data_off = (header_len + 0xFF) & ~0xFF
        out = bytearray(b"SARC" + struct.pack("<HHIIHH", 0x14, 0xFEFF, data_off + len(blob), data_off, 0x100, 0))
        out += sfat + sfnt_blk
        out += b"\0" * (data_off - len(out))
        out += blob
        return bytes(out)
