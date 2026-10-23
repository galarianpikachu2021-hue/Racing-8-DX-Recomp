#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Adds a native-hook check to every block of a generated `main` module (a copy; the source folder is not touched).

    add_block_hooks.py <exefs dir> <new exefs dir>

Each generated block function gets, right after its code-guard line,

    if (__builtin_expect(g_main_hookmap[<off>>2], 0) && recomp_block_hook(c, <off>ULL)) return;

`g_main_hookmap` has one byte per instruction word of the module (set by the host for every address it wants a hook on, all of them
when coverage tracing); `recomp_block_hook` (defined by the host) runs the registered hooks and returns non-zero when one replaced the
block, in which case it has set `c->pc` and the dispatcher carries on from there. Cost with no hook set: one byte load and a branch.
The other modules (rtld, sdk, subsdk0) are hard-linked unchanged.
"""
import os
import re
import shutil
import sys

src, dst = sys.argv[1], sys.argv[2]
if os.path.exists(dst):
    sys.exit(f"{dst} exists")
os.makedirs(dst)
for entry in os.listdir(src):
    s, d = os.path.join(src, entry), os.path.join(dst, entry)
    if entry == "main":
        continue
    if os.path.isdir(s):
        shutil.copytree(s, d, copy_function=os.link, symlinks=True)
    else:
        os.link(s, d) if not os.path.islink(s) else os.symlink(os.readlink(s), d)
msrc, mdst = os.path.join(src, "main"), os.path.join(dst, "main")
shutil.copytree(msrc, mdst, copy_function=os.link, symlinks=True, ignore=shutil.ignore_patterns("src"))
os.makedirs(os.path.join(mdst, "src"))

guard = re.compile(r"(void blk_main_([0-9a-f]{16})\(GuestContext\* c\)\{\n    static const uint32_t _expected\[\]=\{[^\n]*\n    RECOMP_GG_GUARD\([^\n]*\n)")
total = 0
for name in sorted(os.listdir(os.path.join(msrc, "src"))):
    text = open(os.path.join(msrc, "src", name), encoding="utf-8").read()

    def add(m):
        global total
        total += 1
        off = int(m.group(2), 16)
        return (m.group(1) + f"    if (__builtin_expect(g_main_hookmap[0x{off >> 2:x}U], 0) && recomp_block_hook(c, 0x{off:x}ULL)) return;\n")

    new = guard.sub(add, text)
    open(os.path.join(mdst, "src", name), "w", encoding="utf-8").write(new)
print("blocks hooked:", total)

# Runtime: the map and the declarations.
rt_h = os.path.join(mdst, "recomp_runtime.h")
rt_c = os.path.join(mdst, "recomp_runtime.c")
for p in (rt_h, rt_c):
    if os.path.islink(p) or os.stat(p).st_nlink > 1:  # un-link before editing
        data = open(p, "rb").read()
        os.remove(p)
        open(p, "wb").write(data)
h = open(rt_h).read()
h = h.replace("extern uint64_t g_module_base;",
              "extern uint64_t g_module_base;\n"
              "/* kart-recomp: native block hooks (see tools/recomp/add_block_hooks.py). */\n"
              "extern uint8_t g_main_hookmap[];\n"
              "int recomp_block_hook(GuestContext* c, uint64_t module_offset);", 1)
open(rt_h, "w").write(h)
c = open(rt_c).read()
c = c.replace("uint64_t g_module_base = 0;", "uint64_t g_module_base = 0;\nuint8_t g_main_hookmap[0x400000]; /* one byte per instruction word, 16 MiB of code */", 1)
open(rt_c, "w").write(c)
# Static-host builds take the module's globals from recomp_export.c instead.
ex = os.path.join(mdst, "recomp_export.c")
if os.path.islink(ex) or os.stat(ex).st_nlink > 1:
    data = open(ex, "rb").read()
    os.remove(ex)
    open(ex, "wb").write(data)
e = open(ex).read()
e = e.replace("uint64_t g_module_base = 0;\n#elif defined(_WIN32)",
              "uint64_t g_module_base = 0;\n/* kart-recomp: native block hooks, one byte per instruction word of the main module. */\n"
              "uint8_t g_main_hookmap[0x400000];\n#elif defined(_WIN32)", 1)
open(ex, "w").write(e)
