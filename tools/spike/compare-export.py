#!/usr/bin/env python3
"""compare-export.py <fresh tree> <reference tree> [modules...]: compares the generated block functions of two export trees, per module.

Block functions (`void blk_<module>_<addr>(GuestContext* c){ ... }`) are hashed by body, so two trees that only differ in how the blocks are
split over files compare equal. Prints, per module: blocks only in one tree, blocks whose body differs (with the first differing line of
the first few), and exits 1 when anything differs, 0 when the trees agree.
"""
import hashlib, os, re, sys, glob

GUARD = re.compile(rb"RECOMP_GG_GUARD\(_expected,\d+U\)")  # slot numbers are renumbered by the unit splitter: not a difference
START = re.compile(rb"^void (blk_\w+)\(GuestContext\*\s*c\)\s*\{")

def bodies(tree, module):
    out = {}
    for path in sorted(glob.glob(os.path.join(tree, module, "src", "*.c"))):
        name, h, head, closed = None, None, [], True
        def flush():
            if name is not None:
                out[name] = (h.digest(), head)
        with open(path, "rb") as f:
            for line in f:
                if b"RECOMP_GG_GUARD(" in line:
                    line = GUARD.sub(b"RECOMP_GG_GUARD(_expected,#)", line)
                m = START.match(line)
                if m:
                    flush()
                    name, h, head, closed = m.group(1), hashlib.blake2b(digest_size=12), [line], False
                    h.update(line)
                elif name is not None and not closed:
                    if line == b"}\n":
                        closed = True  # the function ends here; what follows in the file is the unit's trailer (lookup tables)
                    h.update(line)
                    if len(head) < 3:
                        head.append(line)
        flush()
    return out

def main(argv):
    if len(argv) < 3:
        print(__doc__); return 2
    fresh, ref = argv[1], argv[2]
    modules = argv[3:] or ["rtld", "main", "subsdk0", "sdk"]
    bad = False
    for m in modules:
        a, b = bodies(fresh, m), bodies(ref, m)
        only_a, only_b = sorted(set(a) - set(b)), sorted(set(b) - set(a))
        diff = [k for k in a if k in b and a[k][0] != b[k][0]]
        print(f"{m}: fresh={len(a)} ref={len(b)} only_fresh={len(only_a)} only_ref={len(only_b)} body_diff={len(diff)}")
        for k in diff[:3]:
            la, lb = a[k][1], b[k][1]
            for x, y in zip(la, lb):
                if x != y:
                    print(f"  {k.decode()}:\n    fresh: {x.decode(errors='replace').rstrip()[:200]}\n    ref:   {y.decode(errors='replace').rstrip()[:200]}")
                    break
        if only_a or only_b or diff:
            bad = True
    print("DIFFERENT" if bad else "SAME")
    return 1 if bad else 0

sys.exit(main(sys.argv))
