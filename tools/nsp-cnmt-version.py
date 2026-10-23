#!/usr/bin/env python3
"""Print the real title version from an NSP's (encrypted) .cnmt.nca. Needs your own prod.keys; reads only the small cnmt NCA.
  python3 tools/nsp-cnmt-version.py <file.nsp>
Keys: $PROD_KEYS, else local/suyu-home/suyu/keys/prod.keys.
"""
import struct, sys, os
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
keys = {}
keys_path = os.environ.get("PROD_KEYS") or os.path.join(
    os.path.dirname(__file__), "..", "local", "suyu-home", "suyu", "keys", "prod.keys")
for l in open(keys_path):
    if "=" in l:
        k, v = l.split("=", 1); keys[k.strip()] = bytes.fromhex(v.strip())
ecb = lambda k, d: (lambda c: c.update(d) + c.finalize())(Cipher(algorithms.AES(k), modes.ECB()).decryptor())
def kak(gen):
    kek = ecb(keys[f"master_key_{gen:02x}"], keys["aes_kek_generation_source"])
    return ecb(ecb(kek, keys["key_area_key_application_source"]), keys["aes_key_generation_source"])
def read_nsp_entry(path, suffix):
    f = open(path, "rb"); _, n, sz, _ = struct.unpack("<4sIII", f.read(16)); ent = [struct.unpack("<QQII", f.read(24)) for _ in range(n)]
    st = f.read(sz); base = 16 + 24 * n + sz
    for o, s, no, _ in ent:
        nm = st[no:st.index(b"\0", no)].decode()
        if nm.endswith(suffix): f.seek(base + o); return nm, f.read(s)
def cnmt_version(path):
    nm, nca = read_nsp_entry(path, ".cnmt.nca")
    x = Cipher(algorithms.AES(keys["header_key"][:16] + keys["header_key"][16:32]), modes.XTS(b"\0" * 16)).decryptor()
    hdr = b"".join(Cipher(algorithms.AES(keys["header_key"]), modes.XTS(i.to_bytes(16, "big"))).decryptor().update(nca[i*0x200:(i+1)*0x200]) for i in range(6))
    assert hdr[0x200:0x204] in (b"NCA3", b"NCA2"), hdr[0x200:0x204]
    ctype, gen_old, gen_new = hdr[0x205], hdr[0x206], hdr[0x220]
    gen = max(gen_old, gen_new); gen = gen - 1 if gen > 0 else 0
    rights = hdr[0x230:0x240]; assert rights == b"\0" * 16, "titlekey crypto not handled"
    area = [ecb(kak(gen), hdr[0x300 + 16*i:0x310 + 16*i]) for i in range(4)]
    fsh = hdr[0x400:0x600]; crypt = fsh[4]; start = struct.unpack_from("<II", hdr, 0x240)[0] * 0x200; end = struct.unpack_from("<II", hdr, 0x240)[1] * 0x200
    enc = nca[start:end]
    if crypt == 3:
        ctr_hi = fsh[0x140:0x148][::-1]; out = b""
        c = Cipher(algorithms.AES(area[2]), modes.CTR(ctr_hi + (start >> 4).to_bytes(8, "big"))).decryptor(); dec = c.update(enc)
    elif crypt == 1: dec = enc
    else: raise SystemExit(f"unhandled crypt type {crypt}")
    i = dec.index(b"PFS0"); n, sz = struct.unpack_from("<II", dec, i + 4)
    ent = [struct.unpack_from("<QQII", dec, i + 16 + 24*k) for k in range(n)]; b = i + 16 + 24*n + sz
    for k, (o, s, no, _) in enumerate(ent):
        nmx = dec[i+16+24*n+no:].split(b"\0")[0].decode()
        if nmx.endswith(".cnmt"):
            c = dec[b+o:b+o+s]; tid, ver = struct.unpack_from("<QI", c, 0); typ = c[0xC]
            return nm, f"{tid:016x}", ver, typ
for p in sys.argv[1:]:
    nm, tid, ver, typ = cnmt_version(p)
    print(f"{os.path.basename(p)}\n  cnmt={nm} title={tid} version={ver} (= {ver>>16}<<16) type=0x{typ:02x}")
