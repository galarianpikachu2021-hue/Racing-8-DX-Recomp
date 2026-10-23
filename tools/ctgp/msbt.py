# SPDX-License-Identifier: GPL-3.0-or-later
"""Minimal MSBT (message) reader/writer, UTF-16 little endian, enough to read labels/strings and replace strings in place."""
import struct


class Msbt:
    def __init__(self, data: bytes):
        assert data[:8] == b"MsgStdBn"
        self.bom = data[8:10]
        assert self.bom == b"\xff\xfe", "only little endian MSBT"
        self.encoding = data[0x0C]
        self.version = data[0x0D]
        sections = []
        count = struct.unpack_from("<H", data, 0x0E)[0]
        pos = 0x20
        for _ in range(count):
            tag = data[pos:pos + 4]
            size = struct.unpack_from("<I", data, pos + 4)[0]
            body = data[pos + 16:pos + 16 + size]
            sections.append([tag, body])
            pos += 16 + ((size + 15) & ~15)
        self.sections = sections
        self.labels: dict[str, int] = {}
        self.strings: list[bytes] = []
        self.groups: list[list[tuple[str, int]]] = []
        self._lbl_dirty = False
        for tag, body in sections:
            if tag == b"LBL1":
                n = struct.unpack_from("<I", body, 0)[0]
                for i in range(n):
                    cnt, off = struct.unpack_from("<II", body, 4 + i * 8)
                    p = off
                    group = []
                    for _ in range(cnt):
                        ln = body[p]
                        name = body[p + 1:p + 1 + ln].decode()
                        idx = struct.unpack_from("<I", body, p + 1 + ln)[0]
                        self.labels[name] = idx
                        group.append((name, idx))
                        p += 1 + ln + 4
                    self.groups.append(group)
            elif tag == b"TXT2":
                n = struct.unpack_from("<I", body, 0)[0]
                offs = struct.unpack_from("<%dI" % n, body, 4)
                for i, o in enumerate(offs):
                    e = offs[i + 1] if i + 1 < n else len(body)
                    self.strings.append(body[o:e])

    def text(self, label: str) -> str | None:
        i = self.labels.get(label)
        if i is None:
            return None
        return self.strings[i].decode("utf-16-le", errors="replace")

    def set_text(self, label: str, text: str):
        """Replaces a label's string with plain text (no control tags)."""
        self.strings[self.labels[label]] = (text.encode("utf-16-le") + b"\0\0")

    @staticmethod
    def label_group(name: str, groups: int) -> int:
        h = 0
        for c in name.encode():
            h = (h * 0x492 + c) & 0xFFFFFFFF
        return h % groups

    def add_label(self, name: str, text: str):
        """Adds a new label with plain text (the label must not exist yet)."""
        assert name not in self.labels
        self.strings.append(text.encode("utf-16-le") + b"\0\0")
        idx = len(self.strings) - 1
        self.groups[self.label_group(name, len(self.groups))].append((name, idx))
        self.labels[name] = idx
        self._lbl_dirty = True

    def _lbl1_body(self) -> bytes:
        n = len(self.groups)
        head = struct.pack("<I", n)
        table, entries, pos = b"", b"", 4 + 8 * n
        for group in self.groups:
            table += struct.pack("<II", len(group), pos + len(entries))
            for name, idx in group:
                raw = name.encode()
                entries += bytes([len(raw)]) + raw + struct.pack("<I", idx)
        return head + table + entries

    def write(self) -> bytes:
        out_sections = []
        for tag, body in self.sections:
            if tag == b"LBL1" and self._lbl_dirty:
                body = self._lbl1_body()
            if tag == b"TXT2":
                n = len(self.strings)
                head = struct.pack("<I", n)
                offs, pos = [], 4 + 4 * n
                for s in self.strings:
                    offs.append(pos)
                    pos += len(s)
                body = head + struct.pack("<%dI" % n, *offs) + b"".join(self.strings)
            out_sections.append((tag, body))
        blob = bytearray()
        for tag, body in out_sections:
            blob += tag + struct.pack("<I", len(body)) + b"\0" * 8 + body
            blob += b"\xab" * (((len(body) + 15) & ~15) - len(body))
        header = b"MsgStdBn" + self.bom + b"\0\0" + bytes([self.encoding, self.version]) + struct.pack("<H", len(out_sections)) + b"\0\0"
        header += struct.pack("<I", 0x20 + len(blob)) + b"\0" * 10
        return header + bytes(blob)
