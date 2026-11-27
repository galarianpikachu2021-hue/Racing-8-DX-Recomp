#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Self-test for shader_index and build-list.py on a synthetic RomFS (no game data needed).

    python -I selftest.py [<work dir>]

Uses shader_index (or shader_index.exe) from this folder, or the path in SHADER_INDEX. The synthetic root holds
aligned and unaligned prefixes, a prefix without a valid SPH, two blobs closer than 0x230, a Yaz0 file with
back-references, Yaz0 streams nested two levels deep, a false Yaz0 magic, a truncated stream, an empty file and
key windows clamped at the end of a file. Keys are checked against hashlib.sha1 with both key lengths.
"""
import hashlib
import os
import random
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
NAMES = ('shader_index.exe', 'shader_index') if os.name == 'nt' else ('shader_index', 'shader_index.exe')
EXE = os.environ.get('SHADER_INDEX') or next(
    (p for p in (os.path.join(HERE, n) for n in NAMES) if os.path.isfile(p)), None)
MAGIC = b'\x78\x56\x34\x12' + b'\0' * 0x2C


def run(*a, keylen=None):
    env = {k: v for k, v in os.environ.items() if k != 'SHADER_INDEX_KEYLEN'}
    if keylen:
        env['SHADER_INDEX_KEYLEN'] = keylen
    p = subprocess.run([EXE, *a], capture_output=True, text=True, env=env)
    return p.returncode, p.stdout + p.stderr


def rnd(n, seed):
    return random.Random(seed).getrandbits(8 * n).to_bytes(n, 'little')


def blob(sph, stype, body):
    """Prefix + SPH word (type in bits 0-4, shader type in bits 10-13) + code."""
    return MAGIC + struct.pack('<I', stype << 10 | sph) + body


def put(buf, off, data):
    buf[off:off + len(data)] = data


def yaz0(data):
    """Greedy Yaz0 encoder (latest 3-byte match in the 0x1000 window): emits overlapping and 3-byte back-references."""
    out = bytearray(b'Yaz0' + struct.pack('>I', len(data)) + bytes(8))
    i = 0
    while i < len(data):
        at = len(out)
        out.append(0)
        for bit in range(8):
            if i >= len(data):
                break
            j = data.rfind(data[i:i + 3], max(0, i - 0x1000), i + 2) if i + 3 <= len(data) else -1
            n = 0
            while j >= 0 and n < 0x111 and i + n < len(data) and data[j + n] == data[i + n]:
                n += 1
            if n >= 3:
                d = i - j - 1
                out += bytes([(n - 2) << 4 | d >> 8, d & 0xFF]) if n < 0x12 else bytes([d >> 8, d & 0xFF, n - 0x12])
                i += n
            else:
                out[at] |= 0x80 >> bit
                out.append(data[i])
                i += 1
    return bytes(out)


def stat(out, label):
    return next(l for l in out.splitlines() if l.startswith(label))[len(label):].split()[0]


if not EXE:
    sys.exit(f'shader_index not found in {HERE}: build it there or set SHADER_INDEX')
tmp = None if len(sys.argv) > 1 else tempfile.TemporaryDirectory()
work = sys.argv[1] if len(sys.argv) > 1 else tmp.name
root = os.path.join(work, 'root')

# --- synthetic RomFS: path -> bytes on disk; bufs: index path -> scanned (decompressed) buffer
plain = bytearray(rnd(0x4000, 1))
put(plain, 0x1000, blob(1, 1, rnd(0x300, 2)))
put(plain, 0x1100, blob(2, 5, rnd(0x300, 3)))    # 0x100 after the previous one: its key window overlaps
put(plain, 0x2808, blob(1, 1, rnd(0x40, 4)))     # off the 0x10 grid: counted, not indexed
put(plain, 0x3000, blob(3, 1, rnd(0x40, 5)))     # SPH type 3: counted, not indexed

comp = bytearray(0x6000)                          # zero runs: overlapping and long back-references
r = rnd(0x800, 6)
put(comp, 0x800, r)
put(comp, 0x1400, r)                              # plain back-references 0xc00 back
put(comp, 0x2000, blob(1, 1, rnd(0x400, 7)))
put(comp, 0x3000, blob(2, 5, rnd(0x400, 8)))
put(comp, 0x5000, b'abc' * 0x55)

trunc = rnd(0x200, 9) + blob(1, 4, rnd(0x300, 10)) + rnd(0x800, 11)

inner2 = rnd(0x100, 12) + blob(1, 3, rnd(0x300, 13)) + bytes(0x100)
inner1 = bytearray(rnd(0x800, 14))
put(inner1, 0x200, blob(2, 5, rnd(0x100, 15)))
inner1 += yaz0(inner2) + bytes(0x40)              # nested stream at 0x800
outer = bytearray(rnd(0x100, 16))
put(outer, 0x40, blob(1, 1, rnd(0x80, 17)))
outer += yaz0(inner1)                             # nested stream at 0x100
outer += bytes(-len(outer) % 0x10)
outer += b'Yaz0' + struct.pack('>I', 0x1000) + bytes(8) + bytes([0, 0x10, 0x05]) + rnd(0x20, 18)  # false magic

files = {
    'a/plain.bin': bytes(plain),
    'a/empty.bin': b'',
    'b c/comp.szs': yaz0(bytes(comp)),
    'd/trunc.szs': yaz0(trunc)[:-4],
    'c/outer.sarc': bytes(outer),
}
bufs = {'a/plain.bin': bytes(plain), 'b c/comp.szs': bytes(comp), 'd/trunc.szs': trunc, 'c/outer.sarc': bytes(outer),
        'c/outer.sarc@0x100': bytes(inner1), 'c/outer.sarc@0x100@0x800': inner2}
for n in (0x37, 0x40, 0x81):                      # blob at the end of the file: key window clamped to n bytes
    files[f'e/end{n:x}.bin'] = bufs[f'e/end{n:x}.bin'] = rnd(0x100, n) + blob(2, 1, rnd(n - 4, n + 1))
for rel, data in files.items():
    os.makedirs(os.path.dirname(os.path.join(root, rel)), exist_ok=True)
    with open(os.path.join(root, rel), 'wb') as f:
        f.write(data)

# Independent reading: every-byte search, 0x10-aligned, SPH type 1 or 2.
brute, unaligned, bad_sph = {}, 0, 0
for rel, buf in bufs.items():
    i = buf.find(MAGIC)
    while i >= 0:
        if i + 0x34 <= len(buf):
            w = struct.unpack_from('<I', buf, i + 0x30)[0]
            if i % 0x10:
                unaligned += 1
            elif w & 0x1F in (1, 2):
                brute[(rel, i)] = (buf, (w >> 10) & 0xF)
            else:
                bad_sph += 1
        i = buf.find(MAGIC, i + 1)
assert len(brute) == 11 and unaligned == 1 and bad_sph == 1, (len(brute), unaligned, bad_sph)

for keylen, close in ((None, 1), ('80', 0)):
    klen = int(keylen or '200', 16)
    idx = os.path.join(work, f'k{klen:x}.idx')
    rc, out = run('index', root, idx, keylen=keylen)
    assert rc == 0, out
    print(out)
    index = {}
    for line in open(idx, encoding='utf-8', newline=''):
        assert line.endswith('\n') and not line.endswith('\r\n'), line
        key, rest = line[:-1].split(' ', 1)
        path, off, st = rest.rsplit(' ', 2)
        index[(path, int(off, 16))] = (key, int(st))
    assert set(index) == set(brute), sorted(set(index) ^ set(brute))
    for (rel, off), (buf, st) in brute.items():
        assert index[(rel, off)] == (hashlib.sha1(buf[off + 0x30:off + 0x30 + klen]).hexdigest(), st), (rel, hex(off))
    assert list(index) == sorted(index), 'lines not sorted by path, offset'
    for label, want in (('files scanned', len(files)), ('szs decompressed', 2), ('blobs found', len(brute)),
                        ('unaligned prefixes', 1), ('prefix w/o SPH 1|2', 1), ('blobs <0x230 apart', close),
                        ('nested Yaz0 scanned', 2), ('errors', 1)):
        assert stat(out, label) == str(want), (label, want, out)
    assert '(+1 magics that did not decode)' in out and 'yaz0 error (scanning partial output): d/trunc.szs' in out, out
    print(f'ok  index, key 0x{klen:x}: {len(index)} blobs = brute-force search, keys = hashlib.sha1, counters')

# extract: Yaz0 output vs the header size and the plaintext, nested paths, default and explicit lengths.
x = os.path.join(work, 'x.bin')
for rel, args, want in (('b c/comp.szs', ['0', 'ffffffff'], bytes(comp)),
                        ('c/outer.sarc@0x100@0x800', ['0', 'ffffffff'], inner2),
                        ('a/plain.bin', ['1000'], bytes(plain[0x1000:])),
                        ('a/plain.bin', ['0x1100', '40'], bytes(plain[0x1100:0x1140]))):
    rc, out = run('extract', root, rel, args[0], x, *args[1:])
    assert rc == 0 and open(x, 'rb').read() == want, (rel, args, out)
    print(f'ok  extract {rel} {" ".join(args)}: 0x{len(want):x} bytes')
assert struct.unpack_from('>I', files['b c/comp.szs'], 4)[0] == len(comp)
rc, out = run('extract', root, 'c/outer.sarc@0x4', '0', x)
assert rc == 1 and 'no valid Yaz0 stream at 0x4' in out, out
print('ok  extract of a non-Yaz0 nested offset fails')

# lookup: a hit, then misses whose printed key checks SHA-1 around the 55/56/64-byte padding boundaries.
cap = os.path.join(work, 'cap.bin')
open(cap, 'wb').write(comp[0x3000:0x3400])
rc, out = run('lookup', os.path.join(work, 'k200.idx'), cap)
assert rc == 0 and 'b c/comp.szs 0x3000 5' in out, out
print('ok  lookup:', out.strip())
for n in (1, 55, 56, 63, 64, 65, 119, 120, 128, 0x200, 0x300):
    junk = MAGIC + rnd(n, 100 + n)
    open(cap, 'wb').write(junk)
    rc, out = run('lookup', os.path.join(work, 'k200.idx'), cap)
    assert rc == 2 and f'not found (key {hashlib.sha1(junk[0x30:0x230]).hexdigest()})' in out, (n, out)
print('ok  lookup misses: SHA-1 of 1..0x200-byte windows = hashlib.sha1')

# build-list.py: one pipeline from two files, one through two nested streams, one dropped, one duplicate.
rec = os.path.join(work, 'rec.txt')
os.makedirs(rec + '.code', exist_ok=True)


def stage(t, code):
    sha = hashlib.sha1(code).hexdigest()
    open(os.path.join(rec + '.code', sha + '.bin'), 'wb').write(code)
    return f'{t} {len(code):x} {sha}'


a, b = stage(0, plain[0x1030:0x1100]), stage(4, comp[0x3030:0x3230])
c, d = stage(4, inner2[0x130:0x330]), stage(0, rnd(0x100, 99))
p1 = f'P 0 topo 3 uint 0 st 2 {a} {b} q 1 2'
open(rec, 'w', newline='\n').write(f'# test\n{p1}\n{p1}\nP 0 topo 3 uint 0 st 1 {d} q\nP 0 topo 3 uint 0 st 1 {c} q\n')
lst = os.path.join(work, 'list.txt')
bl = os.path.join(HERE, 'build-list.py')
p = subprocess.run([sys.executable, '-I', bl, rec, root, os.path.join(work, 'k80.idx'), lst, '9.9.9'],
                   capture_output=True, text=True)
assert p.returncode == 0, p.stdout + p.stderr
print(p.stdout)
assert open(lst, newline='').read() == (
    '# kartrecomp pipelines 1 9.9.9\n'
    f'P 0 topo 3 uint 0 st 2 {a} a/plain.bin 1000 {b} b%20c/comp.szs 3000 q 1 2\n'
    f'P 0 topo 3 uint 0 st 1 {c} c/outer.sarc@0x100@0x800 100 q\n'), open(lst).read()
p = subprocess.run([sys.executable, '-I', bl, '--verify', lst, root], capture_output=True, text=True)
assert p.returncode == 0 and 'stage refs OK 3, FAIL 0' in p.stdout, p.stdout + p.stderr
print('ok  build-list.py: list as expected, --verify:', p.stdout.strip())
print('SELFTEST PASS')
