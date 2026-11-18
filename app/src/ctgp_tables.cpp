// SPDX-License-Identifier: GPL-3.0-or-later
#include "ctgp_tables.h"

#include <algorithm>
#include <bit>
#include <cstdint>
#include <cstring>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace {

// ---- plugin facts (CTGP Deluxe 1.1.1, offsets in the NRO image) -----------------------------------------------------------------
// Found by disassembly (docs/CTGP_PORT.md); each is checked against the first instruction word before use, so another plugin
// version fails cleanly instead of producing wrong tables.
constexpr uint32_t kCourseBuilder = 0xb010; ///< void(db*): appends 188 course entries
constexpr uint32_t kCupBuilder = 0x3c70;    ///< void(cupdb*): constructs the 40 cups and files them into the screen rows
constexpr uint32_t kNew = 0x28e54;          ///< operator new(size)
constexpr uint32_t kNewThunk = 0x28ee4;     ///< b operator new
constexpr uint32_t kDbPush = 0xafcc;        ///< db->push(entry*)
constexpr uint32_t kCupAddA = 0x3b98;       ///< cupdb->add_game(id, cup*, top_row)
constexpr uint32_t kCupAddB = 0x3c04;       ///< cupdb->add_booster(id, cup*, top_row)
constexpr uint32_t kCupCtor = 0x3254;       ///< Cup(cup*, icon name, label): {0, label, icon, 0, {}} (NEON zeroing: stubbed)
constexpr uint32_t kMusicBuilder = 0x9c24;  ///< void(db*): the music sets of the tracks that have their own (after the course builder)
constexpr uint32_t kDbLookup = 0x9bf4;      ///< db->entry(&out, id)
constexpr uint32_t kAddMusicSet = 0x246d4;  ///< entry->music_sets.push(set*); set = {record*, 0, record*}, record 0x18 bytes
struct Expect {
    uint32_t at, word;
};
constexpr Expect kExpect[] = {
    {kCourseBuilder, 0xe92d4ff0}, // push {r4-fp, lr}
    {kNew, 0xe3500001},           // cmp r0, #1
    {kNewThunk, 0xeaffffda},      // b 0x28e54
    {kDbPush, 0xe92d4013},        // push {r0, r1, r4, lr}
    {0x2456c, 0xe8800006},        // entry ctor: stm r0, {r1, r2}
    {kCupBuilder, 0xe92d4070},    // push {r4, r5, r6, lr}
    {kCupAddA, 0xe92d40f0},       // push {r4-r7, lr}
    {kCupAddB, 0xe92d40f0},
    {kCupCtor, 0xf2c00050},       // vmov.i32 q8, #0
    {kMusicBuilder, 0xe92d4ff7},  // push {r0-r2, r4-fp, lr}
    {kDbLookup, 0xe5903000},      // ldr r3, [r0]
    {kAddMusicSet, 0xe92d4013},   // push {r0, r1, r4, lr}
};

constexpr uint32_t kHeap = 0x10000000, kStack = 0x20000000, kStackSize = 0x100000, kStubs = 0x0f000000, kReturn = 0x0efffff0;

struct Machine {
    std::vector<uint8_t> image, heap, stack;
    uint32_t r[16]{};
    bool n = false, z = false, c = false, v = false;
    std::map<uint32_t, std::function<void(Machine&)>> stubs; ///< by address: run instead of the code there, then return to lr
    std::string error;

    uint8_t* At(uint32_t a, uint32_t len) {
        if (a + len <= image.size() && a + len >= a) return image.data() + a;
        if (a >= kHeap && a - kHeap + len <= heap.size()) return heap.data() + (a - kHeap);
        if (a >= kStack && a - kStack + len <= stack.size()) return stack.data() + (a - kStack);
        return nullptr;
    }
    uint32_t Rd32(uint32_t a) {
        const uint8_t* p = At(a, 4);
        if (!p) { Fail(a); return 0; }
        uint32_t v; std::memcpy(&v, p, 4); return v;
    }
    uint16_t Rd16(uint32_t a) {
        const uint8_t* p = At(a, 2);
        if (!p) { Fail(a); return 0; }
        uint16_t v; std::memcpy(&v, p, 2); return v;
    }
    uint8_t Rd8(uint32_t a) {
        const uint8_t* p = At(a, 1);
        if (!p) { Fail(a); return 0; }
        return *p;
    }
    void Wr32(uint32_t a, uint32_t v) { if (uint8_t* p = At(a, 4)) std::memcpy(p, &v, 4); else Fail(a); }
    void Wr16(uint32_t a, uint16_t v) { if (uint8_t* p = At(a, 2)) std::memcpy(p, &v, 2); else Fail(a); }
    void Wr8(uint32_t a, uint8_t v) { if (uint8_t* p = At(a, 1)) *p = v; else Fail(a); }
    void Fail(uint32_t a) {
        if (error.empty()) error = "bad memory access at " + std::to_string(a) + " (pc " + std::to_string(r[15]) + ")";
    }
    uint32_t Alloc(uint32_t n) {
        const uint32_t at = static_cast<uint32_t>((heap.size() + 15) & ~size_t(15));
        heap.resize(at + std::max<uint32_t>(n, 1));
        return kHeap + at;
    }
    std::string Str(uint32_t a, size_t max = 64) {
        std::string s;
        for (size_t i = 0; i < max; ++i) {
            const uint8_t* p = At(a + static_cast<uint32_t>(i), 1);
            if (!p || !*p) break;
            s.push_back(static_cast<char>(*p));
        }
        return s;
    }

    bool Cond(uint32_t cond) const {
        switch (cond) {
        case 0: return z;
        case 1: return !z;
        case 2: return c;
        case 3: return !c;
        case 4: return n;
        case 5: return !n;
        case 6: return v;
        case 7: return !v;
        case 8: return c && !z;
        case 9: return !c || z;
        case 10: return n == v;
        case 11: return n != v;
        case 12: return !z && n == v;
        case 13: return z || n != v;
        default: return true;
        }
    }
    static uint32_t Ror(uint32_t x, uint32_t s) { s &= 31; return s ? (x >> s) | (x << (32 - s)) : x; }

    /// Operand 2 of a data-processing instruction (immediate or register shifted by an immediate); sets *carry.
    uint32_t Op2(uint32_t i, bool* carry) {
        if (i & (1u << 25)) {
            const uint32_t rot = ((i >> 8) & 15) * 2, imm = i & 0xff;
            const uint32_t val = Ror(imm, rot);
            *carry = rot ? (val >> 31) & 1 : c;
            return val;
        }
        const uint32_t rm = i & 15;
        uint32_t val = Reg(rm);
        const uint32_t type = (i >> 5) & 3;
        uint32_t amt;
        if (i & 0x10) amt = Reg((i >> 8) & 15) & 0xff;
        else amt = (i >> 7) & 31;
        *carry = c;
        switch (type) {
        case 0: if (amt) { *carry = (val >> (32 - amt)) & 1; val = amt >= 32 ? 0 : val << amt; } break;
        case 1: if (!(i & 0x10) && amt == 0) amt = 32; if (amt) { *carry = amt <= 32 ? (val >> (amt - 1)) & 1 : 0; val = amt >= 32 ? 0 : val >> amt; } break;
        case 2: if (!(i & 0x10) && amt == 0) amt = 32; if (amt) { *carry = (static_cast<int32_t>(val) >> std::min<uint32_t>(amt - 1, 31)) & 1; val = static_cast<uint32_t>(static_cast<int32_t>(val) >> std::min<uint32_t>(amt, 31)); } break;
        case 3: if (amt) { val = Ror(val, amt); *carry = val >> 31; } else if (!(i & 0x10)) { const uint32_t cin = c; *carry = val & 1; val = (cin << 31) | (val >> 1); } break;
        }
        return val;
    }
    uint32_t Reg(uint32_t n_) const { return n_ == 15 ? r[15] + 8 : r[n_]; }

    /// Runs from `fn` until it returns to kReturn. False on error (unsupported instruction, bad access, step limit).
    bool Call(uint32_t fn, std::initializer_list<uint32_t> args) {
        uint32_t k = 0;
        for (uint32_t a : args) r[k++] = a;
        r[13] = kStack + kStackSize - 0x100;
        r[14] = kReturn;
        r[15] = fn;
        for (uint64_t steps = 0; steps < 20'000'000; ++steps) {
            if (!error.empty()) return false;
            const uint32_t pc = r[15];
            if (pc == kReturn) return true;
            if (auto s = stubs.find(pc); s != stubs.end()) {
                s->second(*this);
                r[15] = r[14];
                continue;
            }
            if (!Step()) return false;
        }
        error = "step limit";
        return false;
    }

    bool Step() {
        const uint32_t pc = r[15];
        const uint32_t i = Rd32(pc);
        if (!error.empty()) return false;
        const uint32_t cond = i >> 28;
        uint32_t next = pc + 4;
        if (cond == 15) { error = "unsupported instruction (unconditional space) at " + std::to_string(pc); return false; }
        if (!Cond(cond)) { r[15] = next; return true; }
        const uint32_t op = (i >> 25) & 7;
        auto set_pc = [&](uint32_t v) { next = v & ~3u; };

        if ((i & 0x0ffffff0) == 0x012fff10) { set_pc(Reg(i & 15)); r[15] = next; return true; }                 // BX
        if ((i & 0x0ffffff0) == 0x012fff30) { r[14] = pc + 4; set_pc(Reg(i & 15)); r[15] = next; return true; }  // BLX reg
        if ((i & 0x0ff00000) == 0x03000000 || (i & 0x0ff00000) == 0x03400000) {                                  // MOVW / MOVT
            const uint32_t rd = (i >> 12) & 15, imm = ((i >> 4) & 0xf000) | (i & 0xfff);
            r[rd] = (i & 0x00400000) ? ((r[rd] & 0xffff) | (imm << 16)) : imm;
            r[15] = next;
            return true;
        }
        if ((i & 0x0fc000f0) == 0x00000090) { // MUL / MLA
            const uint32_t rd = (i >> 16) & 15, rn = (i >> 12) & 15, rs = (i >> 8) & 15, rm = i & 15;
            uint32_t res = Reg(rm) * Reg(rs);
            if (i & (1u << 21)) res += Reg(rn);
            r[rd] = res;
            if (i & (1u << 20)) { n = res >> 31; z = res == 0; }
            r[15] = next;
            return true;
        }
        if (op == 0 && (i & 0x90) == 0x90 && (i & 0x60) != 0) { // halfword / signed / doubleword transfers
            const bool p = i & (1u << 24), u = i & (1u << 23), imm = i & (1u << 22), w = i & (1u << 21), l = i & (1u << 20);
            const uint32_t rn = (i >> 16) & 15, rt = (i >> 12) & 15, sh = (i >> 5) & 3;
            const uint32_t off = imm ? (((i >> 4) & 0xf0) | (i & 15)) : Reg(i & 15);
            const uint32_t base = Reg(rn), addr = p ? (u ? base + off : base - off) : base;
            if (l) {
                if (sh == 1) r[rt] = Rd16(addr);
                else if (sh == 2) r[rt] = static_cast<uint32_t>(static_cast<int8_t>(Rd8(addr)));
                else r[rt] = static_cast<uint32_t>(static_cast<int16_t>(Rd16(addr)));
            } else {
                if (sh == 1) Wr16(addr, static_cast<uint16_t>(r[rt]));
                else if (sh == 2) { r[rt] = Rd32(addr); r[rt + 1] = Rd32(addr + 4); }       // LDRD
                else { Wr32(addr, r[rt]); Wr32(addr + 4, r[rt + 1]); }                      // STRD
            }
            if (!p || w) r[rn] = u ? base + off : base - off;
            r[15] = next;
            return error.empty();
        }
        if (op == 0 || op == 1) { // data processing
            const uint32_t opc = (i >> 21) & 15, rn = (i >> 16) & 15, rd = (i >> 12) & 15;
            const bool s = i & (1u << 20);
            bool carry = c;
            const uint32_t b = Op2(i, &carry), a = Reg(rn);
            uint64_t wide = 0;
            uint32_t res = 0;
            bool write = true, arith = false, cout = carry, vout = v;
            switch (opc) {
            case 0: res = a & b; break;
            case 1: res = a ^ b; break;
            case 2: wide = uint64_t(a) + uint32_t(~b) + 1; res = uint32_t(wide); arith = true; cout = wide >> 32; vout = ((a ^ b) & (a ^ res)) >> 31; break;
            case 3: wide = uint64_t(b) + uint32_t(~a) + 1; res = uint32_t(wide); arith = true; cout = wide >> 32; vout = ((b ^ a) & (b ^ res)) >> 31; break;
            case 4: wide = uint64_t(a) + b; res = uint32_t(wide); arith = true; cout = wide >> 32; vout = (~(a ^ b) & (a ^ res)) >> 31; break;
            case 5: wide = uint64_t(a) + b + c; res = uint32_t(wide); arith = true; cout = wide >> 32; vout = (~(a ^ b) & (a ^ res)) >> 31; break;
            case 6: wide = uint64_t(a) + uint32_t(~b) + c; res = uint32_t(wide); arith = true; cout = wide >> 32; vout = ((a ^ b) & (a ^ res)) >> 31; break;
            case 7: wide = uint64_t(b) + uint32_t(~a) + c; res = uint32_t(wide); arith = true; cout = wide >> 32; vout = ((b ^ a) & (b ^ res)) >> 31; break;
            case 8: res = a & b; write = false; break;
            case 9: res = a ^ b; write = false; break;
            case 10: wide = uint64_t(a) + uint32_t(~b) + 1; res = uint32_t(wide); arith = true; cout = wide >> 32; vout = ((a ^ b) & (a ^ res)) >> 31; write = false; break;
            case 11: wide = uint64_t(a) + b; res = uint32_t(wide); arith = true; cout = wide >> 32; vout = (~(a ^ b) & (a ^ res)) >> 31; write = false; break;
            case 12: res = a | b; break;
            case 13: res = b; break;
            case 14: res = a & ~b; break;
            case 15: res = ~b; break;
            }
            if (s || !write) { n = res >> 31; z = res == 0; c = cout; if (arith) v = vout; }
            if (write) {
                if (rd == 15) { set_pc(res); r[15] = next; return true; }
                r[rd] = res;
            }
            r[15] = next;
            return true;
        }
        if (op == 2 || op == 3) { // LDR / STR / LDRB / STRB
            if (op == 3 && (i & 0x10)) { error = "unsupported media instruction at " + std::to_string(pc); return false; }
            const bool p = i & (1u << 24), u = i & (1u << 23), byte = i & (1u << 22), w = i & (1u << 21), l = i & (1u << 20);
            const uint32_t rn = (i >> 16) & 15, rt = (i >> 12) & 15;
            uint32_t off;
            if (op == 2) off = i & 0xfff;
            else { bool dummy; off = Op2(i & ~(1u << 25) & ~0x10u, &dummy); }
            const uint32_t base = Reg(rn), addr = p ? (u ? base + off : base - off) : base;
            if (l) {
                const uint32_t val = byte ? Rd8(addr) : Rd32(addr);
                if (rt == 15) set_pc(val);
                else r[rt] = val;
            } else {
                const uint32_t val = Reg(rt);
                if (byte) Wr8(addr, static_cast<uint8_t>(val)); else Wr32(addr, val);
            }
            if ((!p || w) && !(l && rn == rt)) r[rn] = u ? base + off : base - off;
            r[15] = next;
            return error.empty();
        }
        if (op == 4) { // LDM / STM
            const bool p = i & (1u << 24), u = i & (1u << 23), w = i & (1u << 21), l = i & (1u << 20);
            const uint32_t rn = (i >> 16) & 15, list = i & 0xffff;
            const uint32_t count = static_cast<uint32_t>(std::popcount(list)); // std::popcount: __builtin_popcount does not exist on MSVC
            const uint32_t base = r[rn];
            uint32_t addr = u ? base : base - 4 * count;
            if (p == u) addr += 4;
            for (uint32_t k = 0; k < 16; ++k) {
                if (!(list & (1u << k))) continue;
                if (l) {
                    const uint32_t val = Rd32(addr);
                    if (k == 15) set_pc(val); else r[k] = val;
                } else Wr32(addr, Reg(k));
                addr += 4;
            }
            if (w && !(l && (list & (1u << rn)))) r[rn] = u ? base + 4 * count : base - 4 * count;
            r[15] = next;
            return error.empty();
        }
        if (op == 5) { // B / BL
            int32_t off = static_cast<int32_t>(i << 8) >> 6;
            if (i & (1u << 24)) r[14] = pc + 4;
            r[15] = pc + 8 + off;
            return true;
        }
        error = "unsupported instruction " + std::to_string(i) + " at " + std::to_string(pc);
        return false;
    }
};

bool LoadNro(Machine& m, const QByteArray& nro, QString* why) {
    const auto* d = reinterpret_cast<const uint8_t*>(nro.constData());
    const size_t len = static_cast<size_t>(nro.size());
    auto u32 = [&](size_t off) { uint32_t v = 0; if (off + 4 <= len) std::memcpy(&v, d + off, 4); return v; };
    if (len < 0x80 || std::memcmp(d + 0x10, "NRO0", 4) != 0) { *why = QStringLiteral("not an NRO file"); return false; }
    uint32_t end = 0;
    for (size_t h : {0x20u, 0x28u, 0x30u}) end = std::max(end, u32(h) + u32(h + 4));
    const uint32_t bss = u32(0x38);
    if (end > len || end > 0x01000000) { *why = QStringLiteral("damaged NRO header"); return false; }
    m.image.assign(d, d + end);
    m.image.resize(end + bss);
    m.stack.resize(kStackSize);
    // Dynamic section: relative relocations (base 0 = no change) and the imports, which get stub addresses.
    const uint32_t mod0 = u32(4);
    if (m.image.size() < mod0 + 8 || std::memcmp(m.image.data() + mod0, "MOD0", 4) != 0) { *why = QStringLiteral("no MOD0"); return false; }
    const uint32_t dyn = mod0 + u32(mod0 + 4);
    std::map<uint32_t, uint32_t> tags;
    for (uint32_t p = dyn; p + 8 <= end; p += 8) {
        const uint32_t t = m.Rd32(p), val = m.Rd32(p + 4);
        if (t == 0) break;
        tags.emplace(t, val);
    }
    const uint32_t symtab = tags[6], strtab = tags[5];
    uint32_t stub = kStubs;
    for (auto [off, size] : {std::pair{tags[17], tags[18]}, std::pair{tags[23], tags[2]}}) {
        for (uint32_t k = 0; off && k + 8 <= size; k += 8) {
            const uint32_t at = m.Rd32(off + k), info = m.Rd32(off + k + 4), type = info & 0xff, sym = info >> 8;
            if (type == 23) continue; // R_ARM_RELATIVE with base 0: the stored value is already right
            const uint32_t name_off = m.Rd32(symtab + sym * 16), value = m.Rd32(symtab + sym * 16 + 4);
            const uint16_t shndx = m.Rd16(symtab + sym * 16 + 14);
            const std::string name = m.Str(strtab + name_off);
            uint32_t target = value;
            if (shndx == 0) {
                target = stub;
                stub += 4;
                if (name == "malloc") m.stubs[target] = [](Machine& mm) { mm.r[0] = mm.Alloc(mm.r[0]); };
                else if (name == "memset") m.stubs[target] = [](Machine& mm) { for (uint32_t q = 0; q < mm.r[2]; ++q) mm.Wr8(mm.r[0] + q, uint8_t(mm.r[1])); };
                else if (name == "memcpy" || name == "memmove") m.stubs[target] = [](Machine& mm) {
                    std::vector<uint8_t> tmp(mm.r[2]);
                    for (uint32_t q = 0; q < mm.r[2]; ++q) tmp[q] = mm.Rd8(mm.r[1] + q);
                    for (uint32_t q = 0; q < mm.r[2]; ++q) mm.Wr8(mm.r[0] + q, tmp[q]);
                };
                else if (name == "strlen") m.stubs[target] = [](Machine& mm) { mm.r[0] = static_cast<uint32_t>(mm.Str(mm.r[0], 4096).size()); };
                else if (name == "free") m.stubs[target] = [](Machine&) {};
                else m.stubs[target] = [name](Machine& mm) { if (mm.error.empty()) mm.error = "the plugin called " + name; };
            }
            if (type == 21 || type == 22) m.Wr32(at, target);
            else if (type == 2) m.Wr32(at, m.Rd32(at) + target);
        }
    }
    if (!m.error.empty()) { *why = QString::fromStdString(m.error); return false; }
    return true;
}

} // namespace

CtgpTablesResult ReadCtgpTables(const QByteArray& nro) {
    CtgpTablesResult res;
    Machine m;
    if (!LoadNro(m, nro, &res.error)) return res;
    for (const Expect& e : kExpect) {
        if (m.Rd32(e.at) != e.word) {
            res.error = QStringLiteral("This CTGP DX version is not supported (only 1.1.1 is).");
            return res;
        }
    }
    // Course database: {u32 count, vector<Entry*>*}; the push is stubbed, so only the entries matter.
    std::vector<uint32_t> entries;
    m.stubs[kNew] = [](Machine& mm) { mm.r[0] = mm.Alloc(mm.r[0]); };
    m.stubs[kDbPush] = [&entries](Machine& mm) { entries.push_back(mm.r[1]); };
    const uint32_t db = m.Alloc(16);
    if (!m.Call(kCourseBuilder, {db})) {
        res.error = QStringLiteral("Reading the CTGP DX course table failed: %1").arg(QString::fromStdString(m.error));
        return res;
    }
    // Music sets (song name / prefix / artist / sub labels per set: CTGPDX OST, Author's Pick), built after the courses.
    struct MusicRec { int course; uint32_t set; };
    std::vector<MusicRec> music;
    std::map<uint32_t, int> entry_id;
    for (size_t k = 0; k < entries.size(); ++k) entry_id[entries[k]] = static_cast<int>(k);
    m.stubs[kDbLookup] = [&entries](Machine& mm) { mm.Wr32(mm.r[1], mm.r[2] < entries.size() ? entries[mm.r[2]] : 0); };
    m.stubs[kAddMusicSet] = [&music, &entry_id](Machine& mm) {
        const auto it = entry_id.find(mm.r[0]);
        if (it != entry_id.end()) music.push_back({it->second, mm.r[1]});
    };
    if (!m.Call(kMusicBuilder, {db})) {
        res.error = QStringLiteral("Reading the CTGP DX music table failed: %1").arg(QString::fromStdString(m.error));
        return res;
    }
    // Cups: the constructor (FUN_3254: {0, label, icon, ...}) runs as code; the adds are stubbed and keep the screen rows.
    struct CupRec { uint32_t id, obj; bool booster, top; };
    std::vector<CupRec> cups;
    m.stubs[kCupAddA] = [&cups](Machine& mm) { cups.push_back({mm.r[1] & 0xff, mm.r[2], false, mm.r[3] != 0}); };
    m.stubs[kCupAddB] = [&cups](Machine& mm) { cups.push_back({mm.r[1] & 0xff, mm.r[2], true, mm.r[3] != 0}); };
    m.stubs[kCupCtor] = [](Machine& mm) {
        mm.Wr32(mm.r[0], 0);
        mm.Wr32(mm.r[0] + 4, mm.r[2]);
        mm.Wr32(mm.r[0] + 8, mm.r[1]);
    };
    const uint32_t cupdb = m.Alloc(0x40);
    if (!m.Call(kCupBuilder, {cupdb})) {
        res.error = QStringLiteral("Reading the CTGP DX cup table failed: %1").arg(QString::fromStdString(m.error));
        return res;
    }
    std::map<uint32_t, int> id_of;
    std::string out = "# CTGP DX tables read from the user's CTGPDX.nro (app/src/ctgp_tables.cpp); format: docs/CTGP_PORT.md\n";
    auto sx = [](uint32_t v) { return static_cast<int>(static_cast<int32_t>(v)); };
    auto sb = [](uint8_t v) { return static_cast<int>(static_cast<int8_t>(v)); };
    for (size_t k = 0; k < entries.size(); ++k) {
        const uint32_t e = entries[k];
        id_of[e] = static_cast<int>(k);
        char line[512];
        std::snprintf(line, sizeof line,
                      "course %zu %s %s index=%d base=%d name=%d platform=%d slot=%d author=%d bgm=%d cup=%d b30=%u b31=%u b32=%u flag=%d\n",
                      k, m.Str(m.Rd32(e)).c_str(), m.Str(m.Rd32(e + 4)).c_str(), sx(m.Rd32(e + 0x10)), sx(m.Rd32(e + 0x14)),
                      sx(m.Rd32(e + 0x18)), sx(m.Rd32(e + 0x1c)), sx(m.Rd32(e + 0x20)), sx(m.Rd32(e + 0x24)), sx(m.Rd32(e + 0x28)),
                      sb(m.Rd8(e + 0x33)), m.Rd8(e + 0x30), m.Rd8(e + 0x31), m.Rd8(e + 0x32), sb(m.Rd8(e + 0x34)));
        out += line;
    }
    {
        std::map<int, int> next_set;
        for (const MusicRec& r : music) {
            const uint32_t rec = m.Rd32(r.set);
            std::string line = "music " + std::to_string(r.course) + " " + std::to_string(next_set[r.course]++);
            for (int k = 0; k < 6; ++k) line += " " + std::to_string(sx(m.Rd32(rec + 4 * k)));
            out += line + "\n";
        }
    }
    // A cup's courses are the entries whose cup byte is that cup, in slot order (the plugin links them the same way at start-up).
    std::map<int, std::map<int, int>> cup_courses;
    for (size_t k = 0; k < entries.size(); ++k) {
        const int cup = sb(m.Rd8(entries[k] + 0x33)), slot = sx(m.Rd32(entries[k] + 0x20));
        if (cup >= 0 && slot >= 0) cup_courses[cup][slot] = static_cast<int>(k);
    }
    std::vector<int> rows[4];
    std::vector<CupRec> sorted = cups;
    std::sort(sorted.begin(), sorted.end(), [](const CupRec& a, const CupRec& b) { return a.id < b.id; });
    for (const CupRec& c : sorted) {
        std::string list;
        for (auto [slot, id] : cup_courses[static_cast<int>(c.id)]) list += (list.empty() ? "" : ",") + std::to_string(id);
        out += "cup " + std::to_string(c.id) + " label=" + std::to_string(m.Rd32(c.obj + 4)) + " icon=" + m.Str(m.Rd32(c.obj + 8)) +
               " courses=" + list + "\n";
    }
    for (const CupRec& c : cups) rows[(c.booster ? 2 : 0) + (c.top ? 0 : 1)].push_back(static_cast<int>(c.id));
    const char* row_names[4] = {"rows_a_top", "rows_a_bottom", "rows_b_top", "rows_b_bottom"};
    for (int k = 0; k < 4; ++k) {
        std::string list;
        for (int id : rows[k]) list += (list.empty() ? "" : ",") + std::to_string(id);
        out += std::string(row_names[k]) + " " + list + "\n";
    }
    res.ok = true;
    res.text = QByteArray::fromStdString(out);
    res.courses = static_cast<int>(entries.size());
    res.cups = static_cast<int>(cups.size());
    return res;
}
