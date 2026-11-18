#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Run functions of CTGPDX.nro (32-bit ARM) in the unicorn emulator to read the tables its code builds at start-up
(course database, cup lists, music tables). Research aid for the CTGP DX port; nothing from the plugin is executed natively.

    tools/ctgp/emu_plugin.py <CTGPDX.nro> coursedb     dump the course database (builder at nro 0xb010)
    tools/ctgp/emu_plugin.py <CTGPDX.nro> call <hex fn> [hex args...]   call one function, print r0

Imports (malloc, memcpy, strlen, ...) are replaced with Python stubs; anything else imported stops the run.
Needs `unicorn` and `capstone` (pip).
"""
import struct
import sys

from unicorn import Uc, UcError, UC_ARCH_ARM, UC_MODE_ARM, UC_HOOK_CODE, UC_HOOK_MEM_UNMAPPED
from unicorn.arm_const import *

BASE = 0x0
STUBS = 0x0F000000
HEAP = 0x10000000
HEAP_SIZE = 0x04000000
STACK = 0x20000000
STACK_SIZE = 0x00100000
RET_MAGIC = 0x0EFFFFF0


class Plugin:
    def __init__(self, path):
        d = open(path, "rb").read()
        segs = [struct.unpack_from("<II", d, h) for h in (0x20, 0x28, 0x30)]
        bss = struct.unpack_from("<I", d, 0x38)[0]
        end = max(o + s for o, s in segs) + bss
        size = (end + 0xFFFF) & ~0xFFFF
        self.uc = uc = Uc(UC_ARCH_ARM, UC_MODE_ARM)
        uc.ctl_set_cpu_model(UC_CPU_ARM_CORTEX_A15)
        uc.reg_write(UC_ARM_REG_C1_C0_2, uc.reg_read(UC_ARM_REG_C1_C0_2) | (0xF << 20))  # VFP/NEON access
        uc.reg_write(UC_ARM_REG_FPEXC, 0x40000000)
        uc.mem_map(BASE, size)
        for o, s in segs:
            uc.mem_write(BASE + o, d[o:o + s])
        uc.mem_map(STUBS, 0x10000)
        uc.mem_map(HEAP, HEAP_SIZE)
        uc.mem_map(STACK, STACK_SIZE)
        uc.mem_map(RET_MAGIC & ~0xFFF, 0x1000)
        self.heap = HEAP
        self.img = d
        self.stub_names = {}
        self.hooks = []
        self._relocate(d)
        uc.hook_add(UC_HOOK_CODE, self._stub_hook, begin=STUBS, end=STUBS + 0x10000)
        uc.hook_add(UC_HOOK_MEM_UNMAPPED, self._unmapped)

    def _unmapped(self, uc, access, addr, size, value, user):
        pc = uc.reg_read(UC_ARM_REG_PC)
        print(f"unmapped access {addr:#x} at pc {pc:#x} lr {uc.reg_read(UC_ARM_REG_LR):#x}", file=sys.stderr)
        return False

    def _relocate(self, d):
        mod0 = struct.unpack_from("<I", d, 4)[0]
        dyn = mod0 + struct.unpack_from("<i", d, mod0 + 4)[0]
        tags = {}
        p = dyn
        while True:
            t, v = struct.unpack_from("<II", d, p)
            if t == 0:
                break
            tags.setdefault(t, v)
            p += 8
        symtab, strtab = tags[6], tags[5]
        rels = []
        if 17 in tags:
            rels.append((tags[17], tags[18]))
        if 23 in tags:
            rels.append((tags[23], tags[2]))
        uc = self.uc
        nstub = 0
        for off, sz in rels:
            for i in range(0, sz, 8):
                r_off, info = struct.unpack_from("<II", d, off + i)
                typ, sym = info & 0xFF, info >> 8
                if typ == 23:  # RELATIVE
                    v = struct.unpack("<I", uc.mem_read(BASE + r_off, 4))[0]
                    uc.mem_write(BASE + r_off, struct.pack("<I", v + BASE))
                    continue
                st_name, st_value, st_size, st_info, st_other, st_shndx = struct.unpack_from("<IIIBBH", d, symtab + sym * 16)
                name = d[strtab + st_name:d.index(b"\0", strtab + st_name)].decode()
                if st_shndx != 0:
                    target = BASE + st_value
                else:
                    target = STUBS + nstub * 4
                    self.stub_names[target] = name
                    uc.mem_write(target, struct.pack("<I", 0xE12FFF1E))  # bx lr
                    nstub += 1
                if typ in (21, 22):
                    uc.mem_write(BASE + r_off, struct.pack("<I", target))
                elif typ == 2:
                    v = struct.unpack("<I", uc.mem_read(BASE + r_off, 4))[0]
                    uc.mem_write(BASE + r_off, struct.pack("<I", v + target))

    def malloc(self, n):
        a = (self.heap + 15) & ~15
        self.heap = a + max(n, 1)
        return a

    def cstr(self, a, maxlen=512):
        if not a:
            return None
        b = bytes(self.uc.mem_read(a, maxlen))
        return b[:b.index(b"\0")].decode("utf-8", "replace") if b"\0" in b else b.decode("utf-8", "replace")

    def _stub_hook(self, uc, addr, size, user):
        name = self.stub_names.get(addr)
        r = [uc.reg_read(UC_ARM_REG_R0 + i) for i in range(4)]
        ret = 0
        if name in ("malloc", "_Znwj", "_Znaj"):
            ret = self.malloc(r[0])
        elif name in ("free", "_ZdlPv", "_ZdaPv"):
            ret = 0
        elif name == "memcpy" or name == "memmove":
            uc.mem_write(r[0], bytes(uc.mem_read(r[1], r[2])))
            ret = r[0]
        elif name == "memset":
            uc.mem_write(r[0], bytes([r[1] & 0xFF]) * r[2])
            ret = r[0]
        elif name == "strlen":
            ret = len(self.cstr(r[0], 4096).encode())
        elif name == "strcmp":
            a, b = self.cstr(r[0]), self.cstr(r[1])
            ret = 0 if a == b else (1 if a > b else 0xFFFFFFFF)
        elif name == "memcmp":
            a, b = bytes(uc.mem_read(r[0], r[2])), bytes(uc.mem_read(r[1], r[2]))
            ret = 0 if a == b else (1 if a > b else 0xFFFFFFFF)
        elif name == "strcpy":
            s = self.cstr(r[1], 4096).encode() + b"\0"
            uc.mem_write(r[0], s)
            ret = r[0]
        elif name in ("nnosLockMutex", "nnosUnlockMutex", "pthread_mutex_lock", "pthread_mutex_unlock", "srand"):
            ret = 0
        elif name == "pthread_once":
            ret = 0
        elif name in ("A32HookFunction", "A32InlineHook"):
            self.hooks.append((name, r[0], r[1]))
            ret = 0
        elif name in ("time",):
            ret = 0
        elif name in ("nn::fs::OpenDirectory", "_ZN2nn2fs13OpenDirectoryEPNS0_15DirectoryHandleEPKci"):
            ret = 1  # failure: no SD card folders
        elif name in ("getenv",):
            ret = 0
        elif name == "pthread_key_create":
            self.keys = getattr(self, "keys", {})
            k = len(self.keys) + 1
            self.keys[k] = 0
            uc.mem_write(r[0], struct.pack("<I", k))
            ret = 0
        elif name == "pthread_getspecific":
            ret = getattr(self, "keys", {}).get(r[0], 0)
        elif name == "pthread_setspecific":
            self.keys = getattr(self, "keys", {})
            self.keys[r[0]] = r[1]
            ret = 0
        elif name == "floor":
            import math
            v = struct.unpack("<d", struct.pack("<II", r[0], r[1]))[0]
            lo, hi = struct.unpack("<II", struct.pack("<d", math.floor(v)))
            uc.reg_write(UC_ARM_REG_R1, hi)
            ret = lo
        else:
            print(f"unhandled import {name} called (lr {uc.reg_read(UC_ARM_REG_LR):#x})", file=sys.stderr)
            self.failed = name
            uc.emu_stop()
            return
        uc.reg_write(UC_ARM_REG_R0, ret & 0xFFFFFFFF)

    def call(self, fn, *args, limit=200_000_000):
        uc = self.uc
        sp = STACK + STACK_SIZE - 0x100
        regs = list(args) + [0] * (4 - min(4, len(args)))
        for i, v in enumerate(regs[:4]):
            uc.reg_write(UC_ARM_REG_R0 + i, v & 0xFFFFFFFF)
        extra = list(args[4:])
        sp -= 4 * len(extra)
        for i, v in enumerate(extra):
            uc.mem_write(sp + 4 * i, struct.pack("<I", v & 0xFFFFFFFF))
        uc.reg_write(UC_ARM_REG_SP, sp)
        uc.reg_write(UC_ARM_REG_LR, RET_MAGIC)
        try:
            uc.emu_start(BASE + fn, RET_MAGIC, count=limit)
        except UcError as e:
            print(f"emulation error {e} at pc {uc.reg_read(UC_ARM_REG_PC):#x}", file=sys.stderr)
            raise
        if getattr(self, "failed", None):
            raise RuntimeError(f"stopped at unhandled import {self.failed}")
        return uc.reg_read(UC_ARM_REG_R0)

    def u32(self, a):
        return struct.unpack("<I", self.uc.mem_read(a, 4))[0]


def dump_boot(p):
    """Print the plugin's singletons after start-up: S = FUN_1be0(), course holder = FUN_1c2c(), cup database = S[2]."""
    S = p.call(0x1BE0)
    H = p.call(0x1C2C)
    words = lambda a, n: [p.u32(a + 4 * i) for i in range(n)]
    print(f"# hooks requested: {len(p.hooks)}")
    print(f"S {S:#x}: " + " ".join(f"{w:#x}" for w in words(S, 8)))
    print(f"H {H:#x}: " + " ".join(f"{w:#x}" for w in words(H, 3)))
    db = p.u32(H)
    names = {}
    vec = p.u32(db + 4)
    for i in range((p.u32(vec + 4) - p.u32(vec)) // 4):
        e = p.u32(p.u32(vec) + 4 * i)
        names[e] = p.cstr(p.u32(e), 32)
    cdb = p.u32(S + 8)
    print(f"cupdb {cdb:#x}: " + " ".join(f"{w:#x}" for w in words(cdb, 10)))
    out = p.malloc(4)

    def show(w):
        if w in names:
            return names[w]
        t = p.cstr(w, 48) if (HEAP <= w < HEAP + HEAP_SIZE) else None
        if t and t.isprintable() and len(t) > 1:
            return repr(t)
        return f"{w:#x}"

    for cup in range(0, 64):
        p.uc.mem_write(out, bytes(4))
        p.call(0x356C, cdb, cup, out)  # FUN_1356c(cupdb, cup id, &cup object)
        c = p.u32(out)
        if not c:
            continue
        ws = words(c, 12)
        print(f"cup {cup:2d} label {ws[1]} icon {p.cstr(ws[2], 32)!r} courses {[show(w) for w in ws[3:3 + ws[0]]]}")
        continue
        # deref anything that looks like a std::string / pointer to text inside the cup object
        for k, w in enumerate(ws):
            if HEAP <= w < HEAP + HEAP_SIZE and w not in names:
                inner = words(w, 4)
                print(f"      [{k}] -> " + " ".join(show(x) for x in inner))


def main():
    p = Plugin(sys.argv[1])
    if sys.argv[2] == "call":
        r = p.call(int(sys.argv[3], 16), *[int(x, 16) for x in sys.argv[4:]])
        print(hex(r))
        return
    if sys.argv[2] == "boot":
        # C++ static constructors (.init_array order), then main's set-up minus the Skyline/SD-card parts.
        for fn in (0x2E0, 0x4C4, 0x844, 0x878, 0xA00, 0xB7C, 0x1E48, 0x37D00, 0x40400, 0x4FD4C, 0x61380, 0x6279C, 0x631C8):
            p.call(fn)
        # Game module bases (FUN_3084 in main): point them at a region where every word is "mov r0,#0; bx lr"-style
        # stubs, so the plugin's calls into the game return 0 and its patches land somewhere harmless.
        GAME = 0x40000000
        p.uc.mem_map(GAME, 0x01000000)
        p.uc.mem_map(0x50000000, 0x01000000)
        p.uc.mem_write(GAME, struct.pack("<II", 0xE3A00000, 0xE12FFF1E) * (0x01000000 // 8))
        p.call(0x3084, GAME, 0x50000000, 0x50000000)
        p.call(0x1E14)
        p.call(0x1CB0)
        dump_boot(p)
        return
    if sys.argv[2] == "export":
        # The text tables the engine reads (core/arm/recomp/ctgp_dx.cpp): one line per course id and per cup.
        for fn in (0x2E0, 0x4C4, 0x844, 0x878, 0xA00, 0xB7C, 0x1E48, 0x37D00, 0x40400, 0x4FD4C, 0x61380, 0x6279C, 0x631C8):
            p.call(fn)
        GAME = 0x40000000
        p.uc.mem_map(GAME, 0x01000000)
        p.uc.mem_map(0x50000000, 0x01000000)
        p.uc.mem_write(GAME, struct.pack("<II", 0xE3A00000, 0xE12FFF1E) * (0x01000000 // 8))
        p.call(0x3084, GAME, 0x50000000, 0x50000000)
        p.call(0x1E14)
        p.call(0x1CB0)
        S, H = p.call(0x1BE0), p.call(0x1C2C)
        db = p.u32(H)
        vec = p.u32(db + 4)
        ids = {}
        lines = ["# CTGP DX tables read from the user's CTGPDX.nro (tools/ctgp/emu_plugin.py export); format: docs/CTGP_PORT.md"]
        n = (p.u32(vec + 4) - p.u32(vec)) // 4
        for i in range(n):
            e = p.u32(p.u32(vec) + 4 * i)
            w = [p.u32(e + 4 * k) for k in range(16)]
            ids[e] = i
            b = bytes(p.uc.mem_read(e + 0x30, 8))
            sx = lambda v: v - (1 << 32) if v & 0x80000000 else v
            sb = lambda v: v - 256 if v & 0x80 else v
            lines.append(f"course {i} {p.cstr(w[0], 64)} {p.cstr(w[1], 64)} index={sx(w[4])} base={sx(w[5])} name={sx(w[6])} "
                         f"platform={sx(w[7])} slot={sx(w[8])} author={sx(w[9])} bgm={sx(w[10])} cup={sb(b[3])} b30={b[0]} b31={b[1]} "
                         f"b32={b[2]} flag={sb(b[4])}")
            # music sets (+0x38: vector of 0x18-byte records, one per set): song, prefix, artist, ?, sub name, ?
            v = w[14]
            if v:
                b0, e0 = p.u32(v), p.u32(v + 4)
                for k in range((e0 - b0) // 4):
                    r = p.u32(b0 + 4 * k)
                    for var in range(3):  # each set: one record* per lap variant (normal, second, final), 0 = none
                        rec = p.u32(r + 4 * var)
                        if rec:
                            lines.append(f"music {i} {k} {var} " + " ".join(str(sx(p.u32(rec + 4 * j))) for j in range(6)))
        cdb = p.u32(S + 8)
        out = p.malloc(4)
        for cup in range(0, 64):
            p.uc.mem_write(out, bytes(4))
            p.call(0x356C, cdb, cup, out)
            c = p.u32(out)
            if not c:
                continue
            ws = [p.u32(c + 4 * k) for k in range(8)]
            lines.append(f"cup {cup} label={ws[1]} icon={p.cstr(ws[2], 32)} courses=" + ",".join(str(ids.get(x, -1)) for x in ws[3:3 + ws[0]]))
        # Cup screen order: the cup database keeps two id lists per group (game cups: +0xc / +0x10, booster + CTGP: +0x18 / +0x1c),
        # filled by FUN_3b98 / FUN_3c04 with a row flag; byte vectors {begin, end, cap}.
        for k, name in ((3, "rows_a_top"), (4, "rows_a_bottom"), (6, "rows_b_top"), (7, "rows_b_bottom")):
            v = p.u32(cdb + 4 * k)
            b, e = p.u32(v), p.u32(v + 4)
            lines.append(f"{name} " + ",".join(str(x) for x in bytes(p.uc.mem_read(b, e - b))))
        open(sys.argv[3], "w").write("\n".join(lines) + "\n")
        print(f"wrote {n} courses to {sys.argv[3]}")
        return
    if sys.argv[2] == "coursedb":
        # The database object is {u32 count, std::vector<Entry*>* list}; entries are 0x40-byte structs.
        db, vec = p.malloc(0x10), p.malloc(0x10)
        p.uc.mem_write(db, struct.pack("<II", 0, vec))
        p.uc.mem_write(vec, bytes(12))
        p.call(0xB010, db)
        begin, end = p.u32(vec), p.u32(vec + 4)
        print(f"# vector {begin:#x}..{end:#x}: {(end - begin) // 4} entries")
        for i in range((end - begin) // 4):
            e = p.u32(begin + 4 * i)
            raw = bytes(p.uc.mem_read(e, 0x40))
            words = struct.unpack("<16I", raw)
            # +0 folder, +4 picture name (char*); +8/+0xc heap objects; then ints (see docs/CTGP_PORT.md)
            txt = [repr(p.cstr(words[0], 64)), repr(p.cstr(words[1], 64))]
            for k in (2, 3):
                w = words[k]
                inner = struct.unpack("<4I", bytes(p.uc.mem_read(w, 16))) if HEAP <= w < HEAP + HEAP_SIZE else ()
                txt.append("[" + " ".join(repr(p.cstr(x, 64)) if HEAP <= x < HEAP + HEAP_SIZE else f"{x:#x}" for x in inner) + "]")
            txt += [f"{w:#x}" for w in words[4:]]
            print(f"{i:3d} " + " ".join(txt))


if __name__ == "__main__":
    main()
