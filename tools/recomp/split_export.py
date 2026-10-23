#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Makes a copy of an exported exefs tree whose generated units are split into smaller files, optionally dropping the
native block hooks from `main` and compiling only the hot blocks with full optimisation.

    split_export.py <exefs dir> <new exefs dir> [--parts=4] [--strip-hooks] [--hot=<list>] [--cold-flags=-O0]

Files are hard-linked where unchanged; every module's src/recompiled_<mod>_<n>.c becomes _p0.._p<parts-1> (+ _hot with
--hot), its CMakeLists.txt lists the pieces, the RAM-based job count assumes 2 GB per compile instead of 8 GB, and the
cold pieces get RECOMP_COLD_FLAGS (cache variable, default from --cold-flags) after the normal flags. The hot list has one
block function name per line (blk_<mod>_<16 hex digits>): blocks that ran in a coverage run, or a profile's hot blocks.
"""
import os
import re
import subprocess
import sys

opts = dict(a[2:].split("=", 1) for a in sys.argv[1:] if a.startswith("--") and "=" in a)
args = [a for a in sys.argv[1:] if not a.startswith("--")]
src, dst = (os.path.abspath(a) for a in args[:2])
parts = int(opts.get("parts", 4))
hot = os.path.abspath(opts["hot"]) if "hot" in opts else None
cold_flags = opts.get("cold-flags", "-O0")
strip = "--strip-hooks" in sys.argv
if os.path.exists(dst):
    sys.exit(f"{dst} exists")
subprocess.run(["cp", "-al", src, dst], check=True)
split = os.path.join(os.path.dirname(os.path.abspath(__file__)), "split_units.py")
hook = re.compile(r"^\s*if \(__builtin_expect\(g_main_hookmap\[")
for mod in sorted(os.listdir(dst)):
    sdir = os.path.join(dst, mod, "src")
    cml = os.path.join(dst, mod, "CMakeLists.txt")
    if not os.path.isdir(sdir) or not os.path.exists(cml):
        continue
    units = sorted(f for f in os.listdir(sdir) if re.fullmatch(rf"recompiled_{re.escape(mod)}_\d+\.c", f))
    cold_files = []
    text = open(cml, encoding="utf-8").read()
    for f in units:
        path = os.path.join(sdir, f)
        tmp = path + ".tmp"
        with open(path, encoding="utf-8") as i, open(tmp, "w", encoding="utf-8") as o:
            for line in i:
                if not (strip and mod == "main" and hook.match(line)):
                    o.write(line)
        os.unlink(path)  # a hard link to the source tree: unlink, never write through it
        subprocess.run([sys.executable, split, tmp, sdir, str(parts)] + ([hot] if hot else []), check=True, stdout=subprocess.DEVNULL)
        os.unlink(tmp)
        stem = f[:-2]
        pieces = [f"src/{stem}_p{k}.c" for k in range(parts)]
        cold_files += pieces
        if hot:
            pieces.append(f"src/{stem}_hot.c")
        text = re.sub(rf"src/{re.escape(f)}(?=[\n)])", "\n    ".join(pieces), text)
    text = text.replace('"${_recomp_ram_mb} / 8192"', '"${_recomp_ram_mb} / 2048"')
    if hot and cold_files:
        block = (f'\n# kart-recomp: blocks that never ran in the coverage/profile runs are compiled cheaply.\n'
                 f'set(RECOMP_COLD_FLAGS "{cold_flags}" CACHE STRING "extra flags for the cold pieces of the generated code")\n'
                 f'if(NOT MSVC)\n  separate_arguments(_recomp_cold NATIVE_COMMAND "${{RECOMP_COLD_FLAGS}}")\n'
                 f'  set_source_files_properties(\n    ' + "\n    ".join(cold_files) +
                 f'\n    PROPERTIES COMPILE_OPTIONS "${{_recomp_opt}};${{_recomp_cold}}")\nendif()\n')
        marker = "if(NOT RECOMP_STATIC_ONLY)"
        idx = text.index(marker, text.rindex("set_source_files_properties(${RECOMP_SOURCES}"))
        text = text[:idx] + block + "\n" + text[idx:]
    os.unlink(cml)
    open(cml, "w", encoding="utf-8").write(text)
    print(f"{mod}: {len(units)} units -> {len(cold_files)} cold pieces" + (f" + {len(units)} hot" if hot else ""))
