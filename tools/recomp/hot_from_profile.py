#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""hot_from_profile.py <hot list> <engine binary> <profile>...: adds every recompiled block that has samples in the given CPU
profiles (KARTRECOMP_PROFILE files of a modules build) to the hot list, in place. For an update whose list was carried over from
another version (tools/recomp/map_hot_list.py), the blocks that did not map are compiled at -O0; the ones a race really runs show
up in a profile. Prints how many were added."""
import os, re, subprocess, sys

hot_path, engine, profiles = sys.argv[1], sys.argv[2], sys.argv[3:]
here = os.path.dirname(os.path.abspath(__file__))
hot = set(l.strip() for l in open(hot_path) if l.strip())
before = len(hot)
seen = set()
for p in profiles:
    out = subprocess.run([sys.executable, os.path.join(here, "..", "profile-report.py"), p, engine, "--top=1000000"],
                         capture_output=True, text=True).stdout
    for m in re.finditer(r"%  (blk_[a-z0-9]+_[0-9a-f]{16})$", out, re.M):
        seen.add(m.group(1))
hot |= seen
with open(hot_path, "w") as f:
    f.write("\n".join(sorted(hot)) + "\n")
print(f"{len(seen)} blocks with samples, {len(hot) - before} new; {len(hot)} entries in {hot_path}")
