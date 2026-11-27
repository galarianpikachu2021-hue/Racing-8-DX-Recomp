// SPDX-License-Identifier: GPL-3.0-or-later
// shader_index: index NVN shader code blobs (0x12345678 prefix + Maxwell SPH) in a Mario Kart 8 Deluxe RomFS dump.
// Standard C++17, builds on Linux and Windows: see README.md.
#define _CRT_SECURE_NO_WARNINGS
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

namespace fs = std::filesystem;
using u8 = uint8_t;
using u32 = uint32_t;
using u64 = unsigned long long;  // not uint64_t: printed with %llu

// SHADER_INDEX_KEYLEN (hex): the key window after the SPH start, 0x200 by default (a short one, e.g. 0x80, finds short shaders).
static const size_t PREFIX = 0x30;
static const size_t KEYLEN = [] { const char* v = getenv("SHADER_INDEX_KEYLEN"); return v ? (size_t)strtoull(v, nullptr, 16) : (size_t)0x200; }();

static u32 rd32le(const u8* p) { u32 v; memcpy(&v, p, 4); return v; }
static u32 rd32be(const u8* p) { return (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3]; }

// Standard Yaz0 stream at in[0..n). Returns false on a truncated/corrupt stream (out keeps what was decoded)
// or when the declared size exceeds maxOut.
static bool yaz0(const u8* in, size_t n, std::vector<u8>& out, size_t maxOut = SIZE_MAX) {
    if (n < 0x10 || memcmp(in, "Yaz0", 4)) return false;
    size_t dsz = rd32be(in + 4);
    if (dsz > maxOut) return false;
    out.assign(dsz, 0);
    size_t s = 0x10, d = 0;
    while (d < dsz) {
        if (s >= n) return false;
        u8 code = in[s++];
        for (int b = 0; b < 8 && d < dsz; b++, code <<= 1) {
            if (code & 0x80) {
                if (s >= n) return false;
                out[d++] = in[s++];
            } else {
                if (s + 2 > n) return false;
                u8 b1 = in[s], b2 = in[s + 1];
                s += 2;
                size_t dist = (((size_t)(b1 & 0xf) << 8) | b2) + 1;
                size_t len = b1 >> 4;
                if (len == 0) { if (s >= n) return false; len = (size_t)in[s++] + 0x12; }
                else len += 2;
                if (dist > d) return false;
                if (len > dsz - d) len = dsz - d;
                u8* o = out.data();
                size_t from = d - dist;
                for (size_t i = 0; i < len; i++) o[d + i] = o[from + i];  // overlapping copy, byte by byte
                d += len;
            }
        }
    }
    return true;
}

// Shared by index and extract so offsets always refer to the same bytes.
// Returns 0 ok, 1 read error, 2 yaz0 error. *wasYaz0 set if the file started with "Yaz0".
static int load(const fs::path& p, std::vector<u8>& raw, std::vector<u8>& dec, bool* wasYaz0, const std::vector<u8>** data) {
    *wasYaz0 = false;
    std::error_code ec;
    u64 sz = fs::file_size(p, ec);
    if (ec) return 1;
    raw.resize((size_t)sz);
    std::ifstream f(p, std::ios::binary);
    if (!f || (sz && !f.read((char*)raw.data(), (std::streamsize)sz))) return 1;
    if (sz >= 0x10 && memcmp(raw.data(), "Yaz0", 4) == 0) {
        *wasYaz0 = true;
        *data = &dec;
        return yaz0(raw.data(), raw.size(), dec) ? 0 : 2;
    }
    *data = &raw;
    return 0;
}

// SHA-1 (FIPS 180-4), same digest as hashlib.sha1.
static std::string sha1hex(const u8* p, size_t n) {
    u32 h[5] = {0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476, 0xc3d2e1f0};
    auto rol = [](u32 x, int k) { return x << k | x >> (32 - k); };
    auto block = [&](const u8* m) {
        u32 w[80];
        for (int i = 0; i < 16; i++) w[i] = rd32be(m + 4 * i);
        for (int i = 16; i < 80; i++) w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        u32 a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
        for (int i = 0; i < 80; i++) {
            u32 f = i < 20 ? (b & c) | (~b & d) : i < 40 ? b ^ c ^ d : i < 60 ? (b & c) | (b & d) | (c & d) : b ^ c ^ d;
            u32 k = i < 20 ? 0x5a827999 : i < 40 ? 0x6ed9eba1 : i < 60 ? 0x8f1bbcdc : 0xca62c1d6;
            u32 t = rol(a, 5) + f + e + k + w[i];
            e = d; d = c; c = rol(b, 30); b = a; a = t;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
    };
    size_t i = 0;
    for (; i + 64 <= n; i += 64) block(p + i);
    u8 tail[128] = {};
    size_t r = n - i, tl = r < 56 ? 64 : 128;
    if (r) memcpy(tail, p + i, r);
    tail[r] = 0x80;
    for (int k = 0; k < 8; k++) tail[tl - 1 - k] = (u8)((u64)n * 8 >> (8 * k));
    block(tail);
    if (tl == 128) block(tail + 64);
    static const char* hx = "0123456789abcdef";
    std::string s(40, '0');
    for (int j = 0; j < 20; j++) {
        u8 v = (u8)(h[j / 4] >> (24 - 8 * (j % 4)));
        s[2 * j] = hx[v >> 4]; s[2 * j + 1] = hx[v & 15];
    }
    return s;
}

// fs::u8path is deprecated in C++20; arguments are still read as UTF-8.
static fs::path upath(const std::string& s) {
#ifdef __cpp_char8_t
    return fs::path(std::u8string(s.begin(), s.end()));
#else
    return fs::u8path(s);
#endif
}

static bool isPrefix(const u8* p, size_t avail) {
    if (avail < PREFIX || rd32le(p) != 0x12345678) return false;
    for (size_t i = 4; i < PREFIX; i++) if (p[i]) return false;
    return true;
}

// Key = SHA-1 of the bytes at prefix+0x30 .. prefix+0x230, clamped to the buffer end.
static std::string keyAt(const u8* buf, size_t n, size_t off) {
    size_t st = off + PREFIX, len = std::min(KEYLEN, n - st);
    return sha1hex(buf + st, len);
}

struct Hit { std::string sub; size_t off; int stype; std::string key; };
struct FileRes {
    std::vector<Hit> hits;
    bool yaz = false; int err = 0;
    u64 rawBytes = 0, scanBytes = 0, unaligned = 0, close = 0, nestedOk = 0, nestedBad = 0, badSph = 0;
};

static std::string hex(u64 v) { char t[32]; snprintf(t, sizeof t, "0x%llx", (unsigned long long)v); return t; }

// sub = "" for the file itself, or "@0x<off>@0x<off>..." for Yaz0 streams nested inside it (e.g. SZS in a SARC).
static void scan(const u8* b, size_t n, FileRes& r, const std::string& sub = "", int depth = 0) {
    size_t last = (size_t)-1;
    // Every-byte search for the magic, so prefixes off the 0x10 grid get counted (memchr is SIMD).
    for (const u8* p = b; n >= PREFIX + 4 && p <= b + n - PREFIX - 4;) {
        p = (const u8*)memchr(p, 0x78, (b + n - PREFIX - 4) - p + 1);
        if (!p) break;
        size_t off = p - b;
        if (isPrefix(p, n - off)) {
            if (off & 0xf) r.unaligned++;
            else {
                u32 w = rd32le(p + PREFIX);
                int sph = w & 0x1f, st = (w >> 10) & 0xf;
                if (sph == 1 || sph == 2) {
                    if (last != (size_t)-1 && off - last < PREFIX + KEYLEN) r.close++;
                    last = off;
                    r.hits.push_back({sub, off, st, keyAt(b, n, off)});
                } else r.badSph++;
            }
        }
        p++;
    }
    // Nested Yaz0 at 4-aligned offsets: a stream that decodes cleanly is scanned too; false magics fail to decode.
    if (depth >= 4) return;
    std::vector<u8> dec;
    for (size_t off = 4; off + 0x10 <= n; off += 4)
        if (memcmp(b + off, "Yaz0", 4) == 0) {
            if (yaz0(b + off, n - off, dec, (size_t)1 << 30)) {
                r.nestedOk++; r.scanBytes += dec.size();
                scan(dec.data(), dec.size(), r, sub + "@" + hex(off), depth + 1);
            } else r.nestedBad++;
        }
}

static int cmdIndex(const fs::path& root, const fs::path& outPath) {
    auto t0 = std::chrono::steady_clock::now();
    std::vector<std::pair<std::string, fs::path>> files;
    for (auto& e : fs::recursive_directory_iterator(root))
        if (e.is_regular_file()) files.push_back({fs::relative(e.path(), root).generic_string(), e.path()});
    std::sort(files.begin(), files.end());
    std::vector<FileRes> res(files.size());
    std::atomic<size_t> next{0};
    unsigned nt = std::max(1u, std::thread::hardware_concurrency());
    std::vector<std::thread> th;
    for (unsigned t = 0; t < nt; t++)
        th.emplace_back([&] {
            std::vector<u8> raw, dec;
            for (size_t i; (i = next++) < files.size();) {
                FileRes& r = res[i];
                try {
                    const std::vector<u8>* d = nullptr;
                    r.err = load(files[i].second, raw, dec, &r.yaz, &d);
                    r.rawBytes = raw.size();
                    if (r.err == 1) { fprintf(stderr, "read error: %s\n", files[i].first.c_str()); continue; }
                    if (r.err == 2) fprintf(stderr, "yaz0 error (scanning partial output): %s\n", files[i].first.c_str());
                    r.scanBytes += d->size();
                    scan(d->data(), d->size(), r);
                } catch (const std::exception& e) {  // e.g. bad_alloc: skip the file, keep the run going
                    r = FileRes{}; r.err = 3;
                    fprintf(stderr, "error %s: %s\n", e.what(), files[i].first.c_str());
                }
            }
        });
    for (auto& t : th) t.join();

    std::ofstream o(outPath, std::ios::binary);
    if (!o) { fprintf(stderr, "cannot write %s\n", outPath.string().c_str()); return 1; }
    u64 nYaz = 0, nHits = 0, rawB = 0, scanB = 0, yazIn = 0, yazOut = 0, unal = 0, close = 0, nestOk = 0, nestBad = 0, badSph = 0, errs = 0;
    std::unordered_set<std::string> keys;
    std::map<int, u64> byType;
    for (size_t i = 0; i < files.size(); i++) {
        FileRes& r = res[i];
        nYaz += r.yaz; rawB += r.rawBytes; scanB += r.scanBytes; unal += r.unaligned; close += r.close;
        nestOk += r.nestedOk; nestBad += r.nestedBad; badSph += r.badSph; errs += r.err != 0;
        if (r.yaz) { yazIn += r.rawBytes; yazOut += r.scanBytes; }
        if (r.unaligned) fprintf(stderr, "note: %llu prefix(es) off the 0x10 grid in %s\n", (unsigned long long)r.unaligned, files[i].first.c_str());
        for (auto& h : r.hits) {
            o << h.key << ' ' << files[i].first << h.sub << ' ' << hex(h.off) << ' ' << h.stype << '\n';
            keys.insert(h.key); byType[h.stype]++; nHits++;
        }
    }
    o.close();
    if (!o) { fprintf(stderr, "cannot write %s\n", outPath.string().c_str()); return 1; }
    double sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    printf("files scanned       %zu\n", files.size());
    printf("szs decompressed    %llu (%.1f MB -> %.1f MB)\n", nYaz, yazIn / 1e6, yazOut / 1e6);
    printf("bytes read/scanned  %.1f MB / %.1f MB\n", rawB / 1e6, scanB / 1e6);
    printf("blobs found         %llu\n", nHits);
    printf("distinct keys       %zu\n", keys.size());
    printf("by shader type     "); for (auto& kv : byType) printf(" %d:%llu", kv.first, kv.second); printf("\n");
    printf("unaligned prefixes  %llu (not indexed)\n", unal);
    printf("prefix w/o SPH 1|2  %llu (not indexed)\n", badSph);
    printf("blobs <0x230 apart  %llu (key window overlaps next blob)\n", close);
    printf("nested Yaz0 scanned %llu (+%llu magics that did not decode)\n", nestOk, nestBad);
    printf("errors              %llu\n", errs);
    printf("threads             %u\n", nt);
    printf("seconds             %.2f\n", sec);
    return 0;
}

static int cmdExtract(const fs::path& root, const std::string& rel, const char* offs, const fs::path& outPath, const char* lens) {
    u64 off = strtoull(offs, nullptr, 16), len = lens ? strtoull(lens, nullptr, 16) : 0x10000;
    std::vector<u8> raw, dec;
    const std::vector<u8>* d = nullptr;
    bool yaz;
    size_t at = rel.find('@');  // "<file>@0x<off>@0x<off>" walks into nested Yaz0 streams
    std::string file = rel.substr(0, at);
    int e = load(root / upath(file), raw, dec, &yaz, &d);
    if (e == 1) { fprintf(stderr, "cannot read %s\n", file.c_str()); return 1; }
    if (e == 2) fprintf(stderr, "warning: yaz0 stream truncated/corrupt\n");
    std::vector<u8> nest[2];
    for (int k = 0; at != std::string::npos; k ^= 1) {
        u64 no = strtoull(rel.c_str() + at + 1, nullptr, 16);
        if (no >= d->size() || !yaz0(d->data() + no, d->size() - no, nest[k])) { fprintf(stderr, "no valid Yaz0 stream at %s\n", hex(no).c_str()); return 1; }
        d = &nest[k]; yaz = true;
        at = rel.find('@', at + 1);
    }
    if (off > d->size()) { fprintf(stderr, "offset beyond end (size %s)\n", hex(d->size()).c_str()); return 1; }
    len = std::min<u64>(len, d->size() - off);
    std::ofstream o(outPath, std::ios::binary);
    o.write((const char*)d->data() + off, (std::streamsize)len);
    if (!o) { fprintf(stderr, "cannot write output\n"); return 1; }
    printf("wrote %s bytes from %s%s (size %s)\n", hex(len).c_str(), rel.c_str(), yaz ? " [yaz0-decompressed]" : "", hex(d->size()).c_str());
    return 0;
}

static int cmdLookup(const fs::path& idx, const fs::path& code) {
    std::ifstream c(code, std::ios::binary);
    std::vector<u8> b((std::istreambuf_iterator<char>(c)), {});
    if (b.size() <= PREFIX) { fprintf(stderr, "capture shorter than 0x31 bytes\n"); return 1; }
    if (!isPrefix(b.data(), b.size())) fprintf(stderr, "warning: capture does not start with the 0x12345678 prefix\n");
    if (b.size() < PREFIX + KEYLEN) fprintf(stderr, "warning: capture shorter than 0x230 bytes, key window is truncated and will only match a blob at the end of its file\n");
    std::string key = keyAt(b.data(), b.size(), 0);
    std::ifstream f(idx);
    if (!f) { fprintf(stderr, "cannot read index\n"); return 1; }
    std::string line; int n = 0;
    while (std::getline(f, line))
        if (line.compare(0, 40, key) == 0) { puts(line.c_str()); n++; }
    if (!n) printf("not found (key %s)\n", key.c_str());
    return n ? 0 : 2;
}

static int run(int argc, char** argv) {
    std::string cmd = argc > 1 ? argv[1] : "";
    if (cmd == "index" && argc == 4) return cmdIndex(upath(argv[2]), upath(argv[3]));
    if (cmd == "extract" && (argc == 6 || argc == 7)) return cmdExtract(upath(argv[2]), argv[3], argv[4], upath(argv[5]), argc == 7 ? argv[6] : nullptr);
    if (cmd == "lookup" && argc == 4) return cmdLookup(upath(argv[2]), upath(argv[3]));
    fprintf(stderr,
            "usage:\n"
            "  shader_index index   <romfs root> <index file>\n"
            "  shader_index extract <romfs root> <relative path> <offset hex> <out file> [<length hex>=0x10000]\n"
            "  shader_index lookup  <index file> <code file>\n");
    return 64;
}

int main(int argc, char** argv) {
    try { return run(argc, argv); }
    catch (const std::exception& e) { fprintf(stderr, "fatal: %s\n", e.what()); return 1; }
}
