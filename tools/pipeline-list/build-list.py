#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Turn a pipeline recording into the list shipped for install-time pipeline precompilation.

    python -I build-list.py <recording file> <romfs root> <k80 index> <out list> <game version> [--exefs <nso dir>]
    python -I build-list.py --verify <out list> <romfs root> [--exefs <nso dir>]

RomFS files are read with shader_index (or shader_index.exe) from this folder, or the path in SHADER_INDEX.

Recording: written by the engine with KARTRECOMP_RECORD_PIPELINES=<file>, one `P ...` line per pipeline,
with `<file>.code/<sha1>.bin` holding each stage's code region [data+0x30, data+0x30+size).

Output list: first line `# kartrecomp pipelines 1 <game version>`, then the recording's lines (same order,
identical lines deduped, and lines that differ only in `q` handle values too) where each stage triple `<stage> <size hex> <sha1>` becomes the quintuple
`<stage> <size hex> <sha1> <romfs path> <prefix offset hex>`:
- romfs path: relative, forward slashes, `%` encoded as `%25` then space as `%20` (decode in reverse
  order). A nested Yaz0 stream is `<file>@0x<stream offset>[@0x...]`, as in shader_index.
- prefix offset: bare lowercase hex like the recording's sizes, offset of the 0x30-byte prefix in the
  (decompressed) buffer; the stage code is [offset+0x30, offset+0x30+size), its SHA-1 is <sha1>.
- with --exefs (the setup's export `nso` folder), a stage not found in the RomFS is searched in the executable's
  NSOs main, sdk, subsdk0 in that order: path `exefs:<name>`, buffer = its segments text, ro, data decompressed
  and concatenated (not placed at their memory offsets).
A pipeline with any stage not found is dropped. The list holds no game bytes.
"""
import hashlib
import os
import struct
import subprocess
import sys
import tempfile
import time
from collections import defaultdict

HERE = os.path.dirname(os.path.abspath(__file__))
NAMES = ('shader_index.exe', 'shader_index') if os.name == 'nt' else ('shader_index', 'shader_index.exe')
EXE = os.environ.get('SHADER_INDEX') or next(
    (p for p in (os.path.join(HERE, n) for n in NAMES) if os.path.isfile(p)), None)
SHORT_KEY = 0x80  # KEYLEN of the k80 index
STAGE_AT = 6      # P <motion> topo <t> uint <u> st <n> ...


def load_buffer(root, path, tmpdir):
    """Whole (decompressed) buffer of a RomFS path via shader_index extract; raises on any failure."""
    if not EXE:
        raise RuntimeError(f'shader_index not found in {HERE}: build it there or set SHADER_INDEX')
    out = os.path.join(tmpdir, 'x.bin')
    r = subprocess.run([EXE, 'extract', root, path, '0', out, '7fffffff'], capture_output=True, text=True)
    if r.returncode != 0 or not os.path.exists(out):
        raise RuntimeError(f'extract {path} failed ({r.returncode}): {r.stderr.strip()}')
    with open(out, 'rb') as f:
        buf = f.read()
    os.remove(out)
    if not buf:
        raise RuntimeError(f'extract {path}: empty output')
    return buf


def lz4(src, size):
    """LZ4 block decode; raises on a malformed block."""
    out, i = bytearray(), 0
    while True:
        tok = src[i]
        i += 1
        n = tok >> 4
        if n == 15:
            while True:
                n += src[i]
                i += 1
                if src[i - 1] != 255:
                    break
        out += src[i:i + n]
        i += n
        if i >= len(src):
            break
        off = src[i] | src[i + 1] << 8
        i += 2
        n = tok & 15
        if n == 15:
            while True:
                n += src[i]
                i += 1
                if src[i - 1] != 255:
                    break
        n += 4
        start = len(out) - off
        if off == 0 or start < 0:
            raise ValueError('lz4: bad match offset')
        pat = out[start:start + min(off, n)]
        out += (pat * (n // len(pat) + 1))[:n]  # an offset below the length repeats the pattern
    if len(out) != size:
        raise ValueError(f'lz4: {len(out)} bytes, expected {size}')
    return bytes(out)


assert lz4(bytes([0x22]) + b'ab' + bytes([2, 0, 0x10]) + b'c', 9) == b'ababababc'


def load_nso(path):
    """Segments text, ro, data of an NSO, decompressed and concatenated."""
    b = open(path, 'rb').read()
    if b[:4] != b'NSO0':
        raise RuntimeError(f'{path}: not an NSO')
    flags = struct.unpack_from('<I', b, 0xc)[0]
    segs = []
    for k, h in enumerate((0x10, 0x20, 0x30)):
        fo, _, sz = struct.unpack_from('<III', b, h)
        cs = struct.unpack_from('<I', b, 0x60 + 4 * k)[0]
        d = b[fo:fo + cs]
        segs.append(lz4(d, sz) if flags >> k & 1 else d)
    return b''.join(segs)


def load_any(root, exefs, path, tmpdir):
    if path.startswith('exefs:'):
        if not exefs:
            raise RuntimeError(f'{path}: give --exefs')
        return load_nso(os.path.join(exefs, path[6:]))
    return load_buffer(root, path, tmpdir)


def stage_span(tok, width):
    """(start, count) of the stage tuples of a tokenized P line."""
    if tok[0] != 'P' or tok[2] != 'topo' or tok[4] != 'uint' or tok[STAGE_AT] != 'st':
        raise ValueError('not a pipeline line: ' + ' '.join(tok[:8]))
    n = int(tok[STAGE_AT + 1])
    if tok[STAGE_AT + 2 + width * n] != 'q':
        raise ValueError('stage count mismatch: ' + ' '.join(tok[:8]))
    return STAGE_AT + 2, n


def enc(path):
    return path.replace('%', '%25').replace(' ', '%20')


def dec(path):
    return path.replace('%20', ' ').replace('%25', '%')


def build(rec, root, idx_path, out_path, version, exefs=None):
    t0 = time.time()
    lines = []
    for raw in open(rec, encoding='ascii'):
        if raw.strip() and not raw.startswith('#'):
            lines.append(raw.split())
    stages = {}  # (size hex, sha1) -> code bytes, or None when unusable
    code_dir = rec + '.code'
    for tok in lines:
        s, n = stage_span(tok, 3)
        for k in range(n):
            _, size, sha = tok[s + 3 * k: s + 3 * k + 3]
            if (size, sha) in stages:
                continue
            code = None
            if int(size, 16) and sha != '-':
                p = os.path.join(code_dir, sha + '.bin')
                if os.path.exists(p):
                    code = open(p, 'rb').read()
                    if len(code) != int(size, 16) or hashlib.sha1(code).hexdigest() != sha:
                        print(f'warning: {p} does not match size {size} / its name', file=sys.stderr)
                        code = None
                else:
                    print(f'warning: missing {p}', file=sys.stderr)
            stages[(size, sha)] = code

    index = defaultdict(list)  # short key -> [(path, offset)]
    for raw in open(idx_path, encoding='utf-8'):
        key, rest = raw.rstrip('\n').split(' ', 1)
        path, off, _ = rest.rsplit(' ', 2)
        index[key].append((path, int(off, 16)))

    # path -> [(offset, stage)] to check; paths visited in the per-stage preference order, so the
    # first exact hit of a stage is its preferred one and each path is extracted at most once.
    by_path = defaultdict(list)
    for st, code in stages.items():
        if code is not None and len(code) >= SHORT_KEY:  # shorter code: its k80 key spans the neighbour
            for path, off in index.get(hashlib.sha1(code[:SHORT_KEY]).hexdigest(), ()):
                by_path[path].append((off, st))
    found = {}  # stage -> (path, offset)
    extracted = 0
    with tempfile.TemporaryDirectory() as tmp:
        # shortcut: one extraction at a time (memory-bound by the biggest file); parallelise if runs get long.
        for path in sorted(by_path, key=lambda p: ('@' in p, len(p), p)):
            todo = sorted((off, st) for off, st in by_path[path] if st not in found)
            if not todo:
                continue
            buf = load_buffer(root, path, tmp)
            extracted += 1
            for off, st in todo:
                code = stages[st]
                if st not in found and buf[off + 0x30: off + 0x30 + len(code)] == code:
                    found[st] = (path, off)
            del buf

    exe_found = 0
    if exefs:
        rest = [st for st, code in stages.items() if code is not None and st not in found]
        for name in ('main', 'sdk', 'subsdk0'):
            if not rest:
                break
            buf = load_nso(os.path.join(exefs, name))
            for st in rest:
                at = buf.find(stages[st])
                if 0 <= at < 0x30:
                    print(f'warning: stage {st[1]} at exefs:{name} 0x{at:x}, below the 0x30 prefix: not usable', file=sys.stderr)
                elif at >= 0x30:
                    found[st] = ('exefs:' + name, at - 0x30)
                    exe_found += 1
            rest = [st for st in rest if st not in found]
            del buf

    out, seen, used, dropped = [], set(), set(), 0
    for tok in lines:
        s, n = stage_span(tok, 3)
        quint = []
        for k in range(n):
            stage, size, sha = tok[s + 3 * k: s + 3 * k + 3]
            hit = found.get((size, sha))
            if hit is None:
                break
            quint += [stage, size, sha, enc(hit[0]), format(hit[1], 'x')]
        else:
            new = tok[:s] + quint + tok[s + 3 * n:]
            # self-check: dropping path/offset gives back the recorded line
            s2, n2 = stage_span(new, 5)
            assert new[:s2] + [x for k in range(n2) for x in new[s2 + 5 * k: s2 + 5 * k + 3]] + new[s2 + 5 * n2:] == tok
            line = ' '.join(new)
            # The precompile uses a `q` answer's texture type, not its handle value: lines that differ only in handle
            # values build the same pipeline (checked: preparing them all on top of one of each added 0 bytes to the cache).
            at = s + 5 * n
            assert new[at] == 'q'
            key = list(new)
            for k in range(int(new[at + 1])):
                key[at + 2 + 5 * k + 3] = '*'
            key = ' '.join(key)
            if key not in seen:
                seen.add(key)
                out.append(line)
                used.update(found[(new[s + 5 * k + 1], new[s + 5 * k + 2])][0] for k in range(n))
            continue
        dropped += 1
    with open(out_path, 'w', encoding='ascii', newline='\n') as f:
        f.write(f'# kartrecomp pipelines 1 {version}\n')
        f.writelines(line + '\n' for line in out)

    missing = sorted(int(sz, 16) for sz, _ in (st for st in stages if st not in found))
    no_cand = len(missing) - len({st for v in by_path.values() for _, st in v} - found.keys())
    print(f'pipelines in        {len(lines)}')
    print(f'pipelines out       {len(out)} (dropped {dropped}, {len(lines) - dropped - len(out)} duplicate lines merged)')
    print(f'distinct stages     {len(stages)}')
    print(f'stages found        {len(found)} ({exe_found} in the exefs)')
    print(f'stages not found    {len(missing)} ({no_cand} without a k80 candidate)', end='')
    if missing:
        print(f' (size 0/unknown: {missing.count(0)}; sizes 0x{missing[0]:x}..0x{missing[-1]:x},'
              f' median 0x{missing[len(missing) // 2]:x})', end='')
    print()
    print(f'romfs files used    {len(used)} (extracted {extracted})')
    print(f'seconds             {time.time() - t0:.1f}')


def verify(list_path, root, exefs=None):
    lines = open(list_path, encoding='ascii').read().splitlines()
    if not lines or not lines[0].startswith('# kartrecomp pipelines 1 '):
        print('FAIL: missing header line')
        return 1
    refs = defaultdict(set)  # path -> {(offset, size, sha1)}
    for line in lines[1:]:
        tok = line.split()
        s, n = stage_span(tok, 5)
        for k in range(n):
            _, size, sha, path, off = tok[s + 5 * k: s + 5 * k + 5]
            refs[dec(path)].add((int(off, 16), int(size, 16), sha))
    ok = fail = 0
    with tempfile.TemporaryDirectory() as tmp:
        for path in sorted(refs):
            buf = load_any(root, exefs, path, tmp)
            for off, size, sha in sorted(refs[path]):
                region = buf[off + 0x30: off + 0x30 + size]
                if len(region) == size and hashlib.sha1(region).hexdigest() == sha:
                    ok += 1
                else:
                    fail += 1
                    print(f'FAIL {path} 0x{off:x} size 0x{size:x} {sha}')
            del buf
    print(f'pipelines {len(lines) - 1}, files {len(refs)}, stage refs OK {ok}, FAIL {fail}')
    return 1 if fail else 0


if __name__ == '__main__':
    a = sys.argv[1:]
    exefs = None
    if '--exefs' in a:
        at = a.index('--exefs')
        exefs = a[at + 1] if at + 1 < len(a) else sys.exit(__doc__)
        del a[at:at + 2]
    if len(a) == 3 and a[0] == '--verify':
        sys.exit(verify(a[1], a[2], exefs))
    if len(a) == 5:
        sys.exit(build(*a, exefs))
    sys.exit(__doc__)
