#!/usr/bin/env python3
"""Wrap loose NCA/tik/cert/xml files into a PFS0 .nsp (streamed, low RAM).
  python3 tools/make-nsp.py out.nsp file1 file2 ...
"""
import os, struct, sys, shutil
out, files = sys.argv[1], sys.argv[2:]
names = [os.path.basename(f).encode() + b"\0" for f in files]
strtab = b"".join(names)
strtab += b"\0" * (-(0x10 + 0x18 * len(files) + len(strtab)) % 0x20)
hdr = struct.pack("<4sIII", b"PFS0", len(files), len(strtab), 0)
off, so = 0, 0
for f, n in zip(files, names):
    sz = os.path.getsize(f); hdr += struct.pack("<QQII", off, sz, so, 0); off += sz; so += len(n)
with open(out, "wb") as o:
    o.write(hdr + strtab)
    for f in files:
        with open(f, "rb") as i: shutil.copyfileobj(i, o, 1 << 22)
