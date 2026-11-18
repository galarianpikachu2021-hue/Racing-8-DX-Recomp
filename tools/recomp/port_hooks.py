#!/usr/bin/env python3
"""Find the 4.0.1 (or any other build's) addresses of game hooks written for 3.0.4.

    tools/recomp/port_hooks.py <old main text.bin> <new main text.bin> [--window=N] code:<off> ... data:<off> ...

text.bin is the main module's code as the exporter writes it (<export>/main/data/text.bin, loaded at module offset 0).
code:<off>  an instruction address in the old build: the window of instructions starting there is searched in the new build with
            the address-dependent fields masked (branch targets, ADRP pages, load/store and add offsets).
data:<off>  a data address the old code builds with ADRP + ADD/LDR/STR: every such reference in the old code is matched the same
            way, and the new address is decoded from the matched instruction pair. A result is printed only when the matches agree.
Nothing from the game is written; the output is offsets only.
"""
import struct
import sys

import numpy as np


def load(path):
    b = open(path, "rb").read()
    return np.frombuffer(b[: len(b) // 4 * 4], dtype="<u4").copy()


def mask_of(w):
    """Masks (vectorised) that keep an instruction's opcode and registers but drop address-dependent immediates."""
    m = np.full(w.shape, 0xFFFFFFFF, dtype=np.uint32)
    top6 = w & 0xFC000000
    m[(top6 == 0x14000000) | (top6 == 0x94000000)] = 0xFC000000  # B / BL imm26
    m[(w & 0x9F000000) == 0x90000000] = 0x9F00001F  # ADRP: keep Rd
    m[(w & 0x9F000000) == 0x10000000] = 0x9F00001F  # ADR
    m[(w & 0xFF000010) == 0x54000000] = 0xFF00001F  # B.cond
    m[(w & 0x7E000000) == 0x34000000] = 0xFF00001F  # CBZ / CBNZ
    m[(w & 0x7E000000) == 0x36000000] = 0xFFF8001F  # TBZ / TBNZ
    m[(w & 0x3B000000) == 0x18000000] = 0xFF00001F  # LDR literal
    m[(w & 0x7F800000) == 0x11000000] = 0xFFC003FF  # ADD imm (often the low part of an ADRP address)
    m[(w & 0x3B000000) == 0x39000000] = 0xFFC003FF  # LDR/STR unsigned imm (struct offsets and ADRP low parts)
    return m


def find(old, new, at, window):
    pat = old[at : at + window]
    pm = mask_of(pat)
    nm = mask_of(new)
    n = len(new) - window
    score = np.zeros(n, dtype=np.int32)
    for k in range(window):
        score += ((new[k : k + n] & pm[k] & nm[k : k + n]) == (pat[k] & pm[k] & nm[k : k + n])).astype(np.int32)
    best = int(score.max())
    hits = np.flatnonzero(score == best)
    return best, hits


def adrp_target(pc, w):
    immlo = (w >> 29) & 3
    immhi = (w >> 5) & 0x7FFFF
    imm = (immhi << 2) | immlo
    if imm & (1 << 20):
        imm -= 1 << 21
    return (pc & ~0xFFF) + (imm << 12)


def low12(w):
    """The byte offset an ADD imm / LDR / STR unsigned-imm instruction adds (scaled for loads/stores)."""
    if (w & 0x7F800000) == 0x11000000:
        return ((w >> 10) & 0xFFF) << (12 if (w >> 22) & 1 else 0)
    if (w & 0x3B000000) == 0x39000000:
        size = (w >> 30) & 3
        if (w >> 26) & 1 and ((w >> 23) & 1):  # 128-bit SIMD
            size = 4
        return ((w >> 10) & 0xFFF) << size
    return None


def refs_to(code, target):
    """(index of ADRP, index of the user) pairs in `code` that build `target`."""
    out = []
    adrps = np.flatnonzero((code & 0x9F000000) == 0x90000000)
    for i in adrps:
        w = int(code[i])
        page = adrp_target(i * 4, w)
        if page != (target & ~0xFFF):
            continue
        rd = w & 0x1F
        for j in range(i + 1, min(i + 6, len(code))):
            u = int(code[j])
            lo = low12(u)
            if lo is not None and ((u >> 5) & 0x1F) == rd and page + lo == target:
                out.append((i, j))
                break
    return out


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    window = next((int(a.split("=")[1]) for a in sys.argv if a.startswith("--window=")), 24)
    old, new = load(args[0]), load(args[1])
    for spec in args[2:]:
        kind, off = spec.split(":")
        off = int(off, 16)
        if kind == "code":
            best, hits = find(old, new, off // 4, window)
            where = ", ".join(f"{h * 4:#x}" for h in hits[:4])
            ok = "OK" if len(hits) == 1 and best >= window - 2 else "CHECK"
            print(f"code {off:#x} -> {where}  ({best}/{window} match, {len(hits)} hit(s)) {ok}")
        else:
            found = {}
            for i, j in refs_to(old, off):
                start = max(0, i - window // 2)
                best, hits = find(old, new, start, window)
                if len(hits) != 1 or best < window - 2:
                    continue
                ni, nj = int(hits[0]) + (i - start), int(hits[0]) + (j - start)
                nw, nu = int(new[ni]), int(new[nj])
                if (nw & 0x9F000000) != 0x90000000 or low12(nu) is None:
                    continue
                addr = adrp_target(ni * 4, nw) + low12(nu)
                found[addr] = found.get(addr, 0) + 1
            if not found:
                print(f"data {off:#x} -> no unique match")
                continue
            ranked = sorted(found.items(), key=lambda kv: -kv[1])
            ok = "OK" if len(ranked) == 1 else "CHECK"
            print(f"data {off:#x} -> " + ", ".join(f"{a:#x} ({n} refs)" for a, n in ranked[:3]) + f" {ok}")


if __name__ == "__main__":
    main()
