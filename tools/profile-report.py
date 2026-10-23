#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Summarizes a KARTRECOMP_PROFILE sample file.

    tools/profile-report.py local/game/profile.txt local/game/mk8d [--threads=CPUCore_0,...] [--top=30] [--summary]

For each thread it lists where samples landed (self) and which functions show up
as callers found on the stack (a rough inclusive view). Recompiled guest code is
named blk_<module>_<offset>.

--seconds=<thread> prints that thread's busy % for each second of the window (new files only,
the "S2" sample lines carry timestamps). --hot=<thread> compares the thread's busiest seconds
(top quarter) with its quietest (bottom half) and lists the functions and categories whose share
grew the most, which is where a dip's extra work goes.

--summary prints one line per busy thread with the share of samples in each
category (game code, guest memory helpers, dispatch, nvn-host hooks, ...), which
is the quick way to compare two builds.
"""
import bisect
import collections
import re
import subprocess
import sys


def category(n):
    if re.match(r"recomp_(load|store|ldp|stp)", n):
        return "guest memory helpers"
    if n.startswith("recomp_lookup") or n.startswith("recomp_image_run_slice") or "ArmRecomp" in n:
        return "dispatch"
    if n.startswith("blk_"):
        return "game code"
    if n.startswith("recomp_"):
        return "recomp runtime"
    if any(k in n for k in ("NvnHost", "RecompHooks", "Sp_counted", "VulkanShadow")):
        return "nvn-host hooks"
    if "Kernel::" in n or "Service::" in n:
        return "HLE kernel/services"
    if n.endswith(".so.6") or "vdso" in n or n.startswith("lib"):
        return "libc/vdso"
    return "other host"


def load_symbols(binary):
    out = subprocess.run(["nm", "-C", "--defined-only", "-n", binary], capture_output=True, text=True).stdout
    addrs, names = [], []
    for line in out.splitlines():
        parts = line.split(" ", 2)
        if len(parts) == 3 and parts[1] in "tTwW":
            addrs.append(int(parts[0], 16))
            names.append(parts[2])
    return addrs, names


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    opts = dict(a[2:].split("=", 1) for a in sys.argv[1:] if a.startswith("--") and "=" in a)
    top = int(opts.get("top", 25))
    want = set(opts["threads"].split(",")) if "threads" in opts else None
    threads, maps, samples = {}, [], collections.defaultdict(list)
    times = collections.defaultdict(list)
    for line in open(args[0]):
        if line.startswith("T "):
            _, i, tid, *name = line.split()
            threads[int(i)] = " ".join(name)
        elif line.startswith("M "):
            f = line.split()
            lo, hi = (int(x, 16) for x in f[1].split("-"))
            maps.append((lo, hi, int(f[3], 16), f[-1] if len(f) > 6 else "[anon]"))
        elif line.startswith("S "):
            f = line.split()
            samples[int(f[1])].append([int(x, 16) for x in f[2:]])
        elif line.startswith("S2 "):
            f = line.split()
            samples[int(f[1])].append([int(x, 16) for x in f[3:]])
            times[int(f[1])].append(int(f[2]))
    addrs, names = load_symbols(args[1])
    exe = next(((lo, off) for lo, hi, off, path in maps if path.endswith("mk8d")), None)

    def name(a):
        for lo, hi, off, path in maps:
            if lo <= a < hi:
                if path.endswith("mk8d"):
                    rel = a - lo + off
                    i = bisect.bisect_right(addrs, rel) - 1
                    return names[i] if i >= 0 else hex(rel)
                return path.rsplit("/", 1)[-1]
        return "?"

    by_name = collections.defaultdict(list)
    for t, s in samples.items():
        by_name[threads.get(t, str(t))].append(s)
    def thread_ids(tname):
        return [t for t, n in threads.items() if n == tname and times.get(t)]

    for opt in ("seconds", "hot"):
        if opt not in opts:
            continue
        ids = thread_ids(opts[opt])
        if not ids:
            print("no timestamped samples for", opts[opt])
            return
        t = ids[0]
        per = collections.defaultdict(list)
        for ms, stack in zip(times[t], samples[t]):
            per[ms // 1000].append(stack)
        busy = {sec: len(v) / 10.0 for sec, v in per.items()}  # 1000 samples = 100%
        if opt == "seconds":
            for sec in sorted(busy):
                print(f"{sec:4d}s {busy[sec]:5.1f}% " + "#" * int(busy[sec] / 2))
            return
        order = sorted(busy, key=busy.get)
        quiet = [x for sec in order[: max(1, len(order) // 2)] for x in per[sec]]
        loud = [x for sec in order[-max(1, len(order) // 4):] for x in per[sec]]
        nq = max(1, len(order) // 2)
        nl = max(1, len(order) // 4)
        print(f"{opts[opt]}: busiest {nl} seconds avg {sum(busy[s_] for s_ in order[-nl:]) / nl:.0f}% busy, "
              f"quietest {nq} seconds avg {sum(busy[s_] for s_ in order[:nq]) / nq:.0f}% busy")
        for label, key in (("category", lambda st: category(name(st[0]))), ("function", lambda st: name(st[0]))):
            cl, cq = collections.Counter(key(x) for x in loud), collections.Counter(key(x) for x in quiet)
            # ms of CPU per second of window: samples divided by seconds in the group
            rows = []
            for k in set(cl) | set(cq):
                rows.append((cl[k] / nl - cq[k] / nq, cl[k] / nl, cq[k] / nq, k))
            print(f"  {label}: extra ms per second in the busy seconds")
            for d, a, b, k in sorted(rows, reverse=True)[:top if label == "function" else 8]:
                print(f"    {d:+7.1f}  ({b:6.1f} -> {a:6.1f})  {k[:100]}")
        return

    total_all = sum(len(s) for s in samples.values())
    print(f"{total_all} samples")
    if "--summary" in sys.argv:
        for tname, groups in sorted(by_name.items(), key=lambda kv: -sum(len(g) for g in kv[1]))[:6]:
            flat = [x for g in groups for x in g]
            c = collections.Counter(category(name(s[0])) for s in flat)
            print(f"{tname:16} {len(flat):6}  " +
                  " | ".join(f"{k} {100 * v / len(flat):.1f}%" for k, v in c.most_common()))
        return
    for tname, groups in sorted(by_name.items(), key=lambda kv: -sum(len(g) for g in kv[1])):
        if want and tname not in want:
            continue
        flat = [x for g in groups for x in g]
        if len(flat) < 20:
            continue
        self_c = collections.Counter(name(s[0]) for s in flat)
        incl = collections.Counter()
        for s in flat:
            incl.update(set(name(a) for a in s))
        print(f"\n== {tname} ({len(groups)} thread(s), {len(flat)} samples)")
        print("  self:")
        for n, c in self_c.most_common(top):
            print(f"    {100 * c / len(flat):5.1f}%  {n[:110]}")
        print("  on stack:")
        for n, c in incl.most_common(top):
            print(f"    {100 * c / len(flat):5.1f}%  {n[:110]}")


if __name__ == "__main__":
    main()
