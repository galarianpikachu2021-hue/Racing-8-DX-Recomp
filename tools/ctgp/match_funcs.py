#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Match functions between two builds of the game (e.g. 32-bit 3.0.3 and 64-bit 3.0.4) from FuncFeatures.java output.

    tools/ctgp/match_funcs.py <old features.tsv> <new features.tsv> <out matches.tsv> [query hex offsets of old functions...]

Anchors: functions with a unique, identical set of referenced strings / external symbols on both sides. Then propagation through the
call graph: the callees of a matched pair are aligned in call order (equal counts, or longest common subsequence of already-known
pairs), callers likewise, until nothing new is found. Each match records how it was found; the output is offsets only.
"""
import sys
from collections import defaultdict


class F:
    __slots__ = ("off", "size", "callees", "strs", "ext", "imms", "callers")

    def __init__(self, line):
        p = line.rstrip("\n").split("\t")
        while len(p) < 6:
            p.append("")
        self.off = int(p[0], 16)
        self.size = int(p[1])
        self.callees = [int(x, 16) for x in p[2].split(",") if x]
        self.strs = tuple(sorted(set(s for s in p[3].split("|") if s)))
        self.ext = tuple(sorted(set(x for x in p[4].split(",") if x)))
        self.imms = tuple(sorted(set(p[5].split(",")) - {""}))
        self.callers = []


def load(path):
    fs = {}
    for line in open(path, encoding="utf-8", errors="replace", newline="\n"):
        f = F(line)
        fs[f.off] = f
    for f in fs.values():
        for c in f.callees:
            if c in fs:
                fs[c].callers.append(f.off)
    return fs


def main():
    old, new = load(sys.argv[1]), load(sys.argv[2])
    out = sys.argv[3]
    m, how = {}, {}  # old off -> new off
    rev = {}

    def add(a, b, why):
        if a in m or b in rev:
            return False
        m[a], rev[b], how[a] = b, a, why
        return True

    # 1. unique signatures
    for keyname, key in (("strings", lambda f: (f.strs, f.ext) if f.strs else None),
                         ("strings-only", lambda f: f.strs if len(f.strs) >= 2 else None),
                         ("ext", lambda f: f.ext if len(f.ext) >= 2 else None)):
        a_idx, b_idx = defaultdict(list), defaultdict(list)
        for f in old.values():
            k = key(f)
            if k:
                a_idx[k].append(f.off)
        for f in new.values():
            k = key(f)
            if k:
                b_idx[k].append(f.off)
        for k, la in a_idx.items():
            lb = b_idx.get(k)
            if lb and len(la) == 1 and len(lb) == 1:
                add(la[0], lb[0], keyname)
    print(f"anchors: {len(m)}")

    # 2. propagation through call lists
    def lcs_pairs(xa, xb):
        """Pairs (i, j) aligning two callee lists; known matches must agree; unknown ones fill gaps of equal length."""
        res = []
        # positions of known pairs in order
        known = [(i, xb.index(m[x])) for i, x in enumerate(xa) if x in m and m[x] in xb]
        known = [(i, j) for (i, j) in known]
        # keep an increasing chain
        chain, last = [], -1
        for i, j in known:
            if j > last:
                chain.append((i, j))
                last = j
        bounds = [(-1, -1)] + chain + [(len(xa), len(xb))]
        for (i0, j0), (i1, j1) in zip(bounds, bounds[1:]):
            ga, gb = xa[i0 + 1:i1], xb[j0 + 1:j1]
            if len(ga) == len(gb):
                res.extend(zip(ga, gb))
        return res

    changed, rounds = True, 0
    while changed and rounds < 50:
        changed, rounds = False, rounds + 1
        for a, b in list(m.items()):
            fa, fb = old[a], new.get(b)
            if fb is None:
                continue
            for x, y in lcs_pairs(fa.callees, fb.callees):
                if x in old and y in new and x not in m and y not in rev:
                    # sanity: externals and string sets must not contradict
                    if old[x].strs and new[y].strs and old[x].strs != new[y].strs:
                        continue
                    if add(x, y, f"callee-of {a:x}"):
                        changed = True
            ca = sorted(set(fa.callers))
            cb = sorted(set(fb.callers))
            ua = [x for x in ca if x not in m]
            ub = [y for y in cb if y not in rev]
            if len(ua) == 1 and len(ub) == 1:
                if add(ua[0], ub[0], f"sole-caller-of {a:x}"):
                    changed = True
    print(f"after propagation: {len(m)} of {len(old)} old functions ({rounds} rounds)")

    # 3. similarity scoring for the rest: mapped callees, strings, externals, immediates (Jaccard), size ratio as a weak tie-break.
    #    Candidates are new functions that call at least one of the same (mapped) callees or share a string.
    by_callee, by_str = defaultdict(set), defaultdict(set)
    for f in new.values():
        if f.off in rev:
            continue
        for c in f.callees:
            by_callee[c].add(f.off)
        for t in f.strs:
            by_str[t].add(f.off)

    def jac(a, b):
        a, b = set(a), set(b)
        return len(a & b) / len(a | b) if a or b else 0.0

    for sweep in range(3):
        added = 0
        for a, fa in old.items():
            if a in m:
                continue
            mc = [m[c] for c in fa.callees if c in m]
            cands = set()
            for c in mc:
                cands |= by_callee.get(c, set())
            for t in fa.strs:
                cands |= by_str.get(t, set())
            cands = [c for c in cands if c not in rev]
            if not cands:
                continue
            scored = []
            for b in cands:
                fb = new[b]
                sc = 2 * jac(mc, [c for c in fb.callees if c in rev]) + jac(fa.strs, fb.strs) + 0.5 * jac(fa.ext, fb.ext) + 0.5 * jac(fa.imms, fb.imms)
                ratio = fb.size / max(fa.size, 1)
                if not 0.5 <= ratio <= 2.5:
                    sc -= 0.5
                scored.append((sc, b))
            scored.sort(reverse=True)
            best = scored[0]
            second = scored[1][0] if len(scored) > 1 else 0
            if best[0] >= 1.0 and best[0] - second >= 0.3:
                if add(a, best[1], f"score {best[0]:.2f}"):
                    added += 1
                    for c in new[best[1]].callees:
                        by_callee[c].discard(best[1])
        print(f"scoring sweep {sweep}: +{added}")
        if not added:
            break
    with open(out, "w") as o:
        for a in sorted(m):
            o.write(f"{a:x}\t{m[a]:x}\t{how[a]}\n")
    for q in sys.argv[4:]:
        qa = int(q, 16)
        # the function containing qa
        cont = max((f for f in old if f <= qa), default=None)
        print(f"{qa:x} (fn {cont:x}) -> " + (f"{m[cont]:x} [{how[cont]}]" if cont in m else "unmatched"))


if __name__ == "__main__":
    main()
