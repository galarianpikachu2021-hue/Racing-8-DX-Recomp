#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Builds a linker symbol-ordering file from KARTRECOMP_PROFILE sample files.

    tools/hot-order.py out.txt <binary> <profile> [<profile> ...] [--min=1]

The generated game code is spread over about 200 MB of text, and the hot part (a few thousand block functions out of
hundreds of thousands) is scattered through it, so instruction fetch and the instruction TLB suffer. This lists the
functions the sampler saw on the guest cores, hottest first, for `ld.lld --symbol-ordering-file` (the units must be
compiled with -ffunction-sections). Names are taken from the binary the profiles were recorded with.
"""
import bisect
import collections
import subprocess
import sys


def main():
    opts = dict(a[2:].split("=", 1) for a in sys.argv[1:] if a.startswith("--") and "=" in a)
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    out_path, binary, profiles = args[0], args[1], args[2:]
    nm = subprocess.run(["nm", "--defined-only", "-n", binary], capture_output=True, text=True).stdout
    addrs, names = [], []
    for line in nm.splitlines():
        p = line.split(" ", 2)
        if len(p) == 3 and p[1] in "tTwW":
            addrs.append(int(p[0], 16))
            names.append(p[2])
    counts = collections.Counter()
    for path in profiles:
        threads, maps = {}, []
        for line in open(path):
            if line.startswith("T "):
                _, i, _tid, *n = line.split()
                threads[int(i)] = " ".join(n)
            elif line.startswith("M "):
                f = line.split()
                lo, hi = (int(x, 16) for x in f[1].split("-"))
                maps.append((lo, hi, int(f[3], 16), f[-1] if len(f) > 6 else ""))
        exe = next(m for m in maps if m[3].endswith("mk8d"))
        for line in open(path):
            if not line.startswith(("S ", "S2 ")):
                continue
            f = line.split()
            thread = int(f[1])
            if not threads.get(thread, "").startswith("CPUCore"):
                continue
            pc = int(f[2 if line.startswith("S ") else 3], 16)
            if not exe[0] <= pc < exe[1]:
                continue
            rel = pc - exe[0] + exe[2]
            i = bisect.bisect_right(addrs, rel) - 1
            if i >= 0:
                counts[names[i]] += 1
    mn = int(opts.get("min", 1))
    chosen = [n for n, c in counts.most_common() if c >= mn]
    with open(out_path, "w") as out:
        out.write("\n".join(chosen) + "\n")
    print(f"{len(chosen)} functions, {sum(counts[n] for n in chosen)} of {sum(counts.values())} samples")


if __name__ == "__main__":
    main()
