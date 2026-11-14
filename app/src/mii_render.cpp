// SPDX-License-Identifier: GPL-3.0-or-later
#include "mii_render.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include <QDir>
#include <QFile>
#include <QMatrix4x4>
#include <QPainter>
#include <QVector3D>

#include <zlib.h>

namespace Mii {
namespace {

// Colour tables (sRGB, 0-255), from the vee Mii library (MIT/Apache): the 100 common colours (hair, eyes, eyebrows, mouth,
// beard, glasses), the matching upper-lip colours, the 10 skin colours and the 12 favourite colours.
#include "mii_colors.inc"

enum ShapeKind { kBeard, kFaceLine, kMask, kHatNormal, kHatCap, kForeheadNormal, kForeheadCap, kHairNormal, kHairCap, kGlasses, kNose, kNoseLine };
const int kShapeCount[12] = {4, 12, 12, 132, 132, 132, 132, 132, 132, 1, 18, 18};
enum TextureKind { kTexHat, kTexEye, kTexEyebrow, kTexBeard, kTexWrinkle, kTexMakeup, kTexGlass, kTexMole, kTexMouth, kTexMustache, kTexNoseLine };
const int kTextureCount[11] = {132, 62, 24, 2, 12, 12, 20, 2, 37, 6, 18};
const int kFaceShapes = 12, kHairShapes = 132;

uint32_t U32(const QByteArray& b, qsizetype at) {
    uint32_t v = 0;
    if (at >= 0 && at + 4 <= b.size()) std::memcpy(&v, b.constData() + at, 4);
    return v;
}
uint16_t U16(const QByteArray& b, qsizetype at) {
    uint16_t v = 0;
    if (at >= 0 && at + 2 <= b.size()) std::memcpy(&v, b.constData() + at, 2);
    return v;
}
float F32(const QByteArray& b, qsizetype at) {
    float v = 0;
    if (at >= 0 && at + 4 <= b.size()) std::memcpy(&v, b.constData() + at, 4);
    return v;
}
float Half(uint16_t h) {
    const int e = (h >> 10) & 31, m = h & 1023;
    float v = e == 0 ? std::ldexp(static_cast<float>(m), -24) : e == 31 ? 0.0f : std::ldexp(static_cast<float>(m + 1024), e - 25);
    return (h & 0x8000) ? -v : v;
}

QByteArray Inflate(const QByteArray& file, uint32_t offset, uint32_t packed, uint32_t size) {
    if (size == 0 || size > (64u << 20) || static_cast<quint64>(offset) + packed > static_cast<quint64>(file.size())) return {};
    QByteArray out(static_cast<qsizetype>(size), 0);
    uLongf got = size;
    if (uncompress(reinterpret_cast<Bytef*>(out.data()), &got, reinterpret_cast<const Bytef*>(file.constData() + offset), packed) != Z_OK) return {};
    out.resize(static_cast<qsizetype>(got));
    return out;
}

// The console stores textures "block linear" (tiles of 64 bytes x 8 rows, grouped `block_height` tiles high): back to rows.
QByteArray Deswizzle(const QByteArray& in, uint32_t width, uint32_t height, uint32_t bytes) {
    const uint32_t hh = height + height / 2;
    const uint32_t bh = hh >= 128 ? 16 : hh >= 64 ? 8 : hh >= 32 ? 4 : hh >= 16 ? 2 : 1;
    const uint32_t stride = width * bytes, gobs = (stride + 63) / 64;
    QByteArray out(static_cast<qsizetype>(stride * height), 0);
    for (uint32_t y = 0; y < height; ++y) {
        const uint32_t row = (y / (8 * bh)) * 512 * bh * gobs + ((y % (8 * bh)) / 8) * 512 + ((y % 8) / 2) * 64 + (y % 2) * 16;
        for (uint32_t x = 0; x < stride; ++x) {
            const uint32_t at = row + (x / 64) * 512 * bh + ((x % 64) / 32) * 256 + ((x % 32) / 16) * 32 + (x % 16);
            if (at < static_cast<uint32_t>(in.size())) out[static_cast<qsizetype>(y * stride + x)] = in[static_cast<qsizetype>(at)];
        }
    }
    return out;
}

// One BC4 block (8 bytes): 16 values.
void Bc4Block(const uint8_t* b, uint8_t out[16]) {
    uint8_t p[8] = {b[0], b[1]};
    if (p[0] > p[1]) for (int i = 1; i < 7; ++i) p[i + 1] = static_cast<uint8_t>(((7 - i) * p[0] + i * p[1]) / 7);
    else {
        for (int i = 1; i < 5; ++i) p[i + 1] = static_cast<uint8_t>(((5 - i) * p[0] + i * p[1]) / 5);
        p[6] = 0, p[7] = 255;
    }
    uint64_t bits = 0;
    for (int i = 0; i < 6; ++i) bits |= static_cast<uint64_t>(b[2 + i]) << (8 * i);
    for (int i = 0; i < 16; ++i) out[i] = p[(bits >> (3 * i)) & 7];
}

struct Rgb {
    float r, g, b;
};
Rgb Colour(const unsigned char c[3]) { return {c[0] / 255.0f, c[1] / 255.0f, c[2] / 255.0f}; }
Rgb Common(int i) { return Colour(kCommonColor[std::clamp(i, 0, 99)]); }

enum Mode { kDirect, kLayered, kAlpha, kLuminanceAlpha };
/// A part texture in its final colours (straight alpha).
QImage Modulate(const QImage& tex, Mode mode, Rgb c0 = {}, Rgb c1 = {}, Rgb c2 = {}) {
    if (tex.isNull() || mode == kDirect) return tex;
    QImage out(tex.size(), QImage::Format_RGBA8888);
    for (int y = 0; y < tex.height(); ++y) {
        const uchar* s = tex.constScanLine(y);
        uchar* d = out.scanLine(y);
        for (int x = 0; x < tex.width(); ++x, s += 4, d += 4) {
            float r, g, b, a;
            const float tr = s[0] / 255.0f, tg = s[1] / 255.0f, tb = s[2] / 255.0f;
            if (mode == kLayered) r = tr * c0.r + tg * c1.r + tb * c2.r, g = tr * c0.g + tg * c1.g + tb * c2.g, b = tr * c0.b + tg * c1.b + tb * c2.b, a = s[3] / 255.0f;
            else if (mode == kAlpha) r = c0.r, g = c0.g, b = c0.b, a = tr;
            else r = tr * c0.r, g = tr * c0.g, b = tr * c0.b, a = tg; // luminance in the first channel, alpha in the second
            d[0] = static_cast<uchar>(std::clamp(r, 0.0f, 1.0f) * 255.0f + 0.5f);
            d[1] = static_cast<uchar>(std::clamp(g, 0.0f, 1.0f) * 255.0f + 0.5f);
            d[2] = static_cast<uchar>(std::clamp(b, 0.0f, 1.0f) * 255.0f + 0.5f);
            d[3] = static_cast<uchar>(std::clamp(a, 0.0f, 1.0f) * 255.0f + 0.5f);
        }
    }
    return out;
}

// ---- software rasterizer: perspective, depth buffer, per-pixel light, optional texture and alpha blending
struct Target {
    int w, h;
    std::vector<float> depth;
    std::vector<float> rgba; // straight alpha, 0..1
    Target(int width, int height) : w(width), h(height), depth(static_cast<size_t>(width) * height, 1e30f), rgba(static_cast<size_t>(width) * height * 4, 0.0f) {}
};
struct Material {
    Rgb colour{1, 1, 1};
    const QImage* texture = nullptr; ///< RGBA8888 straight alpha; replaces the colour
    bool blend = false;              ///< drawn over what is there (no depth write)
    bool mirror = false;             ///< texture coordinates repeat mirrored (the face texture holds one half of the face)
    float depth_bias = 0.0f;
};
const QVector3D kLight = QVector3D(-0.35f, 0.45f, 0.82f).normalized();

void Draw(Target& t, const Renderer::Mesh& mesh, const QMatrix4x4& model, const QMatrix4x4& view_proj, const Material& m) {
    if (mesh.Empty()) return;
    struct V {
        float x, y, z, iw;
        QVector3D n;
        float u, v;
    };
    const QMatrix4x4 mvp = view_proj * model;
    const QMatrix3x3 nm = model.normalMatrix();
    std::vector<V> vs(mesh.pos.size());
    for (size_t i = 0; i < mesh.pos.size(); ++i) {
        const QVector4D p = mvp * QVector4D(mesh.pos[i][0], mesh.pos[i][1], mesh.pos[i][2], 1.0f);
        const float iw = p.w() > 1e-6f ? 1.0f / p.w() : 0.0f;
        V& v = vs[i];
        v.x = (p.x() * iw * 0.5f + 0.5f) * static_cast<float>(t.w);
        v.y = (0.5f - p.y() * iw * 0.5f) * static_cast<float>(t.h);
        v.z = p.z() * iw, v.iw = iw;
        if (i < mesh.normal.size()) {
            const auto& n = mesh.normal[i];
            const float* d = nm.constData(); // column-major
            v.n = QVector3D(d[0] * n[0] + d[3] * n[1] + d[6] * n[2], d[1] * n[0] + d[4] * n[1] + d[7] * n[2], d[2] * n[0] + d[5] * n[1] + d[8] * n[2]);
        } else v.n = QVector3D(0, 0, 1);
        if (i < mesh.uv.size()) v.u = mesh.uv[i][0], v.v = mesh.uv[i][1];
        else v.u = v.v = 0;
    }
    const uchar* tex = m.texture && !m.texture->isNull() ? m.texture->constBits() : nullptr;
    const int tw = tex ? m.texture->width() : 0, th = tex ? m.texture->height() : 0;
    const qsizetype tstride = tex ? m.texture->bytesPerLine() : 0;
    for (size_t i = 0; i + 2 < mesh.index.size(); i += 3) {
        if (mesh.index[i] >= vs.size() || mesh.index[i + 1] >= vs.size() || mesh.index[i + 2] >= vs.size()) continue;
        const V& a = vs[mesh.index[i]];
        const V& b = vs[mesh.index[i + 1]];
        const V& c = vs[mesh.index[i + 2]];
        if (a.iw == 0 || b.iw == 0 || c.iw == 0) continue;
        const float area = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
        if (std::fabs(area) < 1e-6f) continue;
        const int x0 = std::max(0, static_cast<int>(std::floor(std::min({a.x, b.x, c.x})))), x1 = std::min(t.w - 1, static_cast<int>(std::ceil(std::max({a.x, b.x, c.x}))));
        const int y0 = std::max(0, static_cast<int>(std::floor(std::min({a.y, b.y, c.y})))), y1 = std::min(t.h - 1, static_cast<int>(std::ceil(std::max({a.y, b.y, c.y}))));
        for (int y = y0; y <= y1; ++y) {
            for (int x = x0; x <= x1; ++x) {
                const float px = static_cast<float>(x) + 0.5f, py = static_cast<float>(y) + 0.5f;
                const float w0 = ((b.x - px) * (c.y - py) - (b.y - py) * (c.x - px)) / area;
                const float w1 = ((c.x - px) * (a.y - py) - (c.y - py) * (a.x - px)) / area;
                const float w2 = 1.0f - w0 - w1;
                if (w0 < 0 || w1 < 0 || w2 < 0) continue;
                const float z = w0 * a.z + w1 * b.z + w2 * c.z - m.depth_bias;
                const size_t at = static_cast<size_t>(y) * t.w + x;
                if (z >= t.depth[at]) continue;
                const float p0 = w0 * a.iw, p1 = w1 * b.iw, p2 = w2 * c.iw, ps = 1.0f / (p0 + p1 + p2);
                QVector3D n = (a.n * p0 + b.n * p1 + c.n * p2) * ps;
                n.normalize();
                if (n.z() < 0) n = -n; // meshes are drawn two-sided
                const float light = std::min(1.0f, 0.70f + 0.36f * std::max(0.0f, QVector3D::dotProduct(n, kLight)));
                float r = m.colour.r, g = m.colour.g, bl = m.colour.b, alpha = 1.0f;
                if (tex) {
                    float u = (a.u * p0 + b.u * p1 + c.u * p2) * ps;
                    const float v = (a.v * p0 + b.v * p1 + c.v * p2) * ps;
                    if (m.mirror) {
                        u = std::fmod(std::fabs(u), 2.0f);
                        if (u > 1.0f) u = 2.0f - u;
                    }
                    const float fx = std::clamp(u, 0.0f, 1.0f) * static_cast<float>(tw) - 0.5f, fy = std::clamp(v, 0.0f, 1.0f) * static_cast<float>(th) - 0.5f;
                    const int ix = std::clamp(static_cast<int>(std::floor(fx)), 0, tw - 1), iy = std::clamp(static_cast<int>(std::floor(fy)), 0, th - 1);
                    const int jx = std::min(ix + 1, tw - 1), jy = std::min(iy + 1, th - 1);
                    const float ax = std::clamp(fx - static_cast<float>(ix), 0.0f, 1.0f), ay = std::clamp(fy - static_cast<float>(iy), 0.0f, 1.0f);
                    float acc[4] = {0, 0, 0, 0};
                    const auto add = [&](int sx, int sy, float w) {
                        const uchar* s = tex + sy * tstride + sx * 4;
                        const float sa = s[3] / 255.0f * w;
                        acc[0] += s[0] / 255.0f * sa, acc[1] += s[1] / 255.0f * sa, acc[2] += s[2] / 255.0f * sa, acc[3] += sa;
                    };
                    add(ix, iy, (1 - ax) * (1 - ay)), add(jx, iy, ax * (1 - ay)), add(ix, jy, (1 - ax) * ay), add(jx, jy, ax * ay);
                    alpha = acc[3];
                    if (alpha <= 0.002f) continue;
                    r = acc[0] / alpha, g = acc[1] / alpha, bl = acc[2] / alpha;
                }
                r *= light, g *= light, bl *= light;
                float* d = &t.rgba[at * 4];
                if (m.blend) {
                    const float da = d[3], oa = alpha + da * (1 - alpha);
                    if (oa <= 0) continue;
                    d[0] = (r * alpha + d[0] * da * (1 - alpha)) / oa, d[1] = (g * alpha + d[1] * da * (1 - alpha)) / oa, d[2] = (bl * alpha + d[2] * da * (1 - alpha)) / oa, d[3] = oa;
                } else {
                    d[0] = r, d[1] = g, d[2] = bl, d[3] = 1.0f;
                    t.depth[at] = z;
                }
            }
        }
    }
}

/// An ellipsoid (the shoulders) or, with `top` < 1, its lower part only.
Renderer::Mesh Ellipsoid(float cx, float cy, float cz, float rx, float ry, float rz) {
    Renderer::Mesh m;
    const int rings = 24, segs = 32;
    for (int i = 0; i <= rings; ++i) {
        const float a = static_cast<float>(M_PI) * static_cast<float>(i) / rings;
        for (int j = 0; j <= segs; ++j) {
            const float b = 2 * static_cast<float>(M_PI) * static_cast<float>(j) / segs;
            const float nx = std::sin(a) * std::cos(b), ny = std::cos(a), nz = std::sin(a) * std::sin(b);
            m.pos.push_back({cx + rx * nx, cy + ry * ny, cz + rz * nz});
            QVector3D n(nx / rx, ny / ry, nz / rz);
            n.normalize();
            m.normal.push_back({n.x(), n.y(), n.z()});
        }
    }
    for (int i = 0; i < rings; ++i)
        for (int j = 0; j < segs; ++j) {
            const auto p = static_cast<uint16_t>(i * (segs + 1) + j), q = static_cast<uint16_t>(p + segs + 1);
            for (uint16_t v : {p, q, static_cast<uint16_t>(p + 1), static_cast<uint16_t>(p + 1), q, static_cast<uint16_t>(q + 1)}) m.index.push_back(v);
        }
    return m;
}

} // namespace

bool Renderer::Load(const QString& dir, QString* error) {
    loaded = false;
    mesh_cache.clear();
    texture_cache.clear();
    const auto fail = [&](const QString& why) {
        if (error) *error = why;
        return false;
    };
    const auto read = [&](const QStringList& names) {
        for (const QString& n : names) {
            QFile f(QDir(dir).filePath(n));
            if (f.open(QIODevice::ReadOnly)) return f.readAll();
        }
        return QByteArray();
    };
    shape_file = read({QStringLiteral("ShapeHigh.dat"), QStringLiteral("ShapeMid.dat")});
    texture_file = read({QStringLiteral("NXTextureMidSRGB.dat"), QStringLiteral("NXTextureLowSRGB.dat")});
    if (shape_file.size() < 0x14dd4 || std::memcmp(shape_file.constData(), "NFSR", 4) != 0) return fail(QStringLiteral("no Mii shape file"));
    if (texture_file.size() < 0x2428 || std::memcmp(texture_file.constData(), "NFTR", 4) != 0) return fail(QStringLiteral("no Mii texture file"));
    qsizetype at = 12 + 12 * 8;
    for (int kind = 0; kind < 12; ++kind) {
        shapes[kind].clear();
        for (int i = 0; i < kShapeCount[kind]; ++i, at += 88) {
            Element e;
            e.offset = U32(shape_file, at), e.size = U32(shape_file, at + 4), e.packed = U32(shape_file, at + 8);
            for (int k = 0; k < 5; ++k) e.attr_offset[k] = U32(shape_file, at + 16 + 4 * k), e.attr_size[k] = U32(shape_file, at + 36 + 4 * k);
            e.index_offset = U32(shape_file, at + 56), e.index_size = U32(shape_file, at + 60);
            shapes[kind].push_back(e);
        }
    }
    at += kHairShapes * 72; // where a hat sits on each hair style: not drawn here
    hair_translate.clear(), nose_translate.clear(), beard_translate.clear();
    for (int i = 0; i < kFaceShapes; ++i, at += 36) {
        const auto v = [&](int k) { return std::array<float, 3>{F32(shape_file, at + 12 * k), F32(shape_file, at + 12 * k + 4), F32(shape_file, at + 12 * k + 8)}; };
        hair_translate.push_back(v(0)), nose_translate.push_back(v(1)), beard_translate.push_back(v(2));
    }
    at = 12 + 11 * 8;
    for (int kind = 0; kind < 11; ++kind) {
        textures[kind].clear();
        for (int i = 0; i < kTextureCount[kind]; ++i, at += 28) {
            Element e;
            e.offset = U32(texture_file, at), e.size = U32(texture_file, at + 4), e.packed = U32(texture_file, at + 8);
            e.width = U16(texture_file, at + 20), e.height = U16(texture_file, at + 22);
            e.format = static_cast<uint8_t>(texture_file[at + 24]), e.tile_mode = static_cast<uint8_t>(texture_file[at + 26]);
            textures[kind].push_back(e);
        }
    }
    loaded = true;
    // Decoded ASTC pictures, in AstcJob's order.
    astc.clear();
    QFile decoded(QDir(dir).filePath(QStringLiteral("astc.rgba")));
    if (decoded.open(QIODevice::ReadOnly)) {
        const QByteArray all = decoded.readAll();
        qsizetype pos = 0;
        for (int kind = 0; kind < 11; ++kind)
            for (size_t i = 0; i < textures[kind].size(); ++i) {
                const Element& e = textures[kind][i];
                if (e.format != 6 || !e.width || !e.height) continue;
                const qsizetype bytes = static_cast<qsizetype>(e.width) * e.height * 4;
                if (pos + bytes > all.size()) return true;
                QImage img(e.width, e.height, QImage::Format_RGBA8888);
                for (int y = 0; y < e.height; ++y) std::memcpy(img.scanLine(y), all.constData() + pos + static_cast<qsizetype>(y) * e.width * 4, static_cast<size_t>(e.width) * 4);
                astc[kind * 1000 + static_cast<int>(i)] = img;
                pos += bytes;
            }
    }
    return true;
}

QByteArray Renderer::AstcJob() const {
    QByteArray job;
    if (!loaded) return job;
    for (int kind = 0; kind < 11; ++kind)
        for (const Element& e : textures[kind]) {
            if (e.format != 6 || !e.width || !e.height) continue;
            const uint32_t size[2] = {e.width, e.height}, bw = (e.width + 3u) / 4, bh = (e.height + 3u) / 4;
            QByteArray data = Inflate(texture_file, e.offset, e.packed, e.size);
            if (e.tile_mode == 0) data = Deswizzle(data, bw, bh, 16);
            data.resize(static_cast<qsizetype>(bw) * bh * 16, 0); // the first level only
            job.append(reinterpret_cast<const char*>(size), 8);
            job.append(data);
        }
    return job;
}

Renderer::Mesh Renderer::LoadMesh(int kind, int index) const {
    if (!loaded || index < 0 || index >= static_cast<int>(shapes[kind].size())) return {};
    const int key = kind * 1000 + index;
    if (const auto it = mesh_cache.find(key); it != mesh_cache.end()) return it->second;
    const Element& e = shapes[kind][static_cast<size_t>(index)];
    Mesh m;
    const QByteArray data = Inflate(shape_file, e.offset, e.packed, e.size);
    const auto fits = [&](uint32_t off, uint32_t size) { return static_cast<quint64>(off) + size <= static_cast<quint64>(data.size()); };
    if (!data.isEmpty() && e.attr_size[0] && fits(e.attr_offset[0], e.attr_size[0]) && fits(e.index_offset, e.index_size)) {
        const uint32_t count = e.attr_size[0] / 8;
        for (uint32_t i = 0; i < count; ++i) {
            const qsizetype p = e.attr_offset[0] + i * 8;
            m.pos.push_back({Half(U16(data, p)), Half(U16(data, p + 2)), Half(U16(data, p + 4))});
        }
        if (e.attr_size[1] >= count * 4 && fits(e.attr_offset[1], count * 4))
            for (uint32_t i = 0; i < count; ++i) {
                const uint32_t v = U32(data, e.attr_offset[1] + i * 4);
                const auto s = [&](int shift) { return static_cast<float>(static_cast<int32_t>(v << (22 - shift)) >> 22) / 511.0f; };
                m.normal.push_back({s(0), s(10), s(20)});
            }
        if (e.attr_size[2] >= count * 4 && fits(e.attr_offset[2], count * 4))
            for (uint32_t i = 0; i < count; ++i) {
                const qsizetype p = e.attr_offset[2] + i * 4;
                m.uv.push_back({Half(U16(data, p)), Half(U16(data, p + 2))});
            }
        for (uint32_t i = 0; i < e.index_size / 2; ++i) m.index.push_back(U16(data, e.index_offset + i * 2));
    }
    mesh_cache[key] = m;
    return m;
}

QImage Renderer::LoadTexture(int kind, int index) const {
    if (!loaded || index < 0 || index >= static_cast<int>(textures[kind].size())) return {};
    const int key = kind * 1000 + index;
    if (const auto it = texture_cache.find(key); it != texture_cache.end()) return it->second;
    const Element& e = textures[kind][static_cast<size_t>(index)];
    QImage img;
    if (e.format == 6) {
        const auto it = astc.find(key);
        return it != astc.end() ? it->second : QImage();
    }
    const uint32_t w = e.width, h = e.height;
    QByteArray data = (w && h && w <= 4096 && h <= 4096) ? Inflate(texture_file, e.offset, e.packed, e.size) : QByteArray();
    if (!data.isEmpty()) {
        const bool blocks = e.format >= 3;
        const uint32_t bw = blocks ? (w + 3) / 4 : w, bh = blocks ? (h + 3) / 4 : h;
        const uint32_t bytes = e.format == 0 ? 1 : e.format == 1 ? 2 : e.format == 2 ? 4 : e.format == 3 ? 8 : 16;
        if (e.tile_mode == 0) data = Deswizzle(data, bw, bh, bytes);
        if (static_cast<quint64>(data.size()) >= static_cast<quint64>(bw) * bh * bytes && e.format <= 4) {
            img = QImage(static_cast<int>(w), static_cast<int>(h), QImage::Format_RGBA8888);
            img.fill(0);
            const auto* src = reinterpret_cast<const uint8_t*>(data.constData());
            const auto put = [&](uint32_t x, uint32_t y, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
                if (x >= w || y >= h) return;
                uchar* d = img.scanLine(static_cast<int>(y)) + x * 4;
                d[0] = r, d[1] = g, d[2] = b, d[3] = a;
            };
            if (!blocks) {
                for (uint32_t y = 0; y < h; ++y)
                    for (uint32_t x = 0; x < w; ++x) {
                        const uint8_t* s = src + (y * w + x) * bytes;
                        if (bytes == 1) put(x, y, s[0], s[0], s[0], 255);
                        else if (bytes == 2) put(x, y, s[0], s[1], 0, 255);
                        else put(x, y, s[0], s[1], s[2], s[3]);
                    }
            } else {
                for (uint32_t by = 0; by < bh; ++by)
                    for (uint32_t bx = 0; bx < bw; ++bx) {
                        const uint8_t* s = src + (by * bw + bx) * bytes;
                        uint8_t r[16], g[16];
                        Bc4Block(s, r);
                        if (e.format == 4) Bc4Block(s + 8, g);
                        for (uint32_t i = 0; i < 16; ++i) {
                            const uint32_t x = bx * 4 + i % 4, y = by * 4 + i / 4;
                            if (e.format == 3) put(x, y, r[i], r[i], r[i], 255);
                            else put(x, y, r[i], g[i], 0, 255);
                        }
                    }
            }
        }
    }
    texture_cache[key] = img;
    return img;
}

// Where the face parts go on the mask: positions are in 1/64ths of the texture, as the console's Mii library computes them.
QImage Renderer::MaskTexture(const StoreData& mii, int size) const {
    QImage mask(size, size, QImage::Format_ARGB32_Premultiplied);
    mask.fill(Qt::transparent);
    QPainter p(&mask);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    const float unit = static_cast<float>(size) / 64.0f, sx = 0.88961464f, sy = 0.9276675f, magic = 1.1600001f;
    const auto dim = [](int scale) { return 1.0f + 0.4f * static_cast<float>(scale); };
    const auto angle = [](int rotate) { return 360.0f / 32.0f * static_cast<float>(rotate % 32); };
    enum Origin { kCentre, kLeft, kRight };
    const auto part = [&](const QImage& img, float x, float y, float w, float h, float deg, Origin origin) {
        if (img.isNull() || w <= 0 || h <= 0) return;
        p.save();
        p.translate(x * unit, y * unit);
        p.rotate(deg);
        p.scale(w * unit * sx, h * unit * sy);
        const float base = origin == kCentre ? -0.5f : origin == kRight ? -1.0f : 0.0f;
        const QImage draw = origin == kLeft ? img.mirrored(true, false) : img;
        p.drawImage(QRectF(base, -0.5, 1.0, 1.0), draw);
        p.restore();
    };
    static const int kEyeRotate[80] = {3, 4, 4, 4, 3, 4, 4, 4, 3, 4, 4, 4, 4, 3, 3, 4, 4, 4, 3, 3, 4, 3, 4, 3, 3, 4, 3,
                                       4, 4, 3, 4, 4, 4, 3, 3, 3, 4, 4, 3, 3, 3, 4, 4, 3, 3, 3, 3, 3, 3, 3, 3, 3, 4, 4,
                                       4, 4, 3, 4, 4, 3, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4};
    static const int kBrowRotate[24] = {26, 26, 27, 25, 26, 25, 26, 25, 28, 25, 26, 24, 27, 27, 26, 26, 25, 25, 26, 26, 27, 26, 25, 27};
    const Rgb black{0, 0, 0}, white{1, 1, 1};
    const auto G = [&](const char* key) { return mii.Get(key); };

    // Mustache (two mirrored halves), mouth, eyebrows, eyes, mole: later parts cover earlier ones.
    {
        const float y = 31.763554f + magic * sy * static_cast<float>(G("mustache_y")), w = 288.0f / 64.0f * dim(G("mustache_scale")), h = 576.0f / 64.0f * dim(G("mustache_scale"));
        const QImage img = Modulate(LoadTexture(kTexMustache, G("mustache_type")), kAlpha, Common(G("beard_color")));
        part(img, 32.0f, y, w, h, 0, kLeft), part(img, 32.0f, y, w, h, 0, kRight);
    }
    {
        const float s = dim(G("mouth_scale")), a = 0.12f * static_cast<float>(G("mouth_aspect")) + 0.64f;
        const int c = std::clamp(G("mouth_color"), 0, 99);
        part(Modulate(LoadTexture(kTexMouth, G("mouth_type")), kLayered, Common(c), Colour(kUpperLipColor[c]), white), 32.0f,
             29.25885f + magic * sy * static_cast<float>(G("mouth_y")), 396.0f / 64.0f * s, 288.0f / 64.0f * s * a, 0, kCentre);
    }
    {
        const int type = std::clamp(G("eyebrow_type"), 0, 23);
        const float s = dim(G("eyebrow_scale")), a = 0.12f * static_cast<float>(G("eyebrow_aspect")) + 0.64f;
        const float dx = sx * static_cast<float>(G("eyebrow_x")), y = 16.549807f + magic * sy * static_cast<float>(G("eyebrow_y"));
        const float w = 324.0f / 64.0f * s, h = 288.0f / 64.0f * s * a, deg = angle(G("eyebrow_rotate") + kBrowRotate[type]);
        const QImage img = Modulate(LoadTexture(kTexEyebrow, type), kAlpha, Common(G("eyebrow_color")));
        part(img, 32.0f + dx, y, w, h, 360.0f - deg, kLeft), part(img, 32.0f - dx, y, w, h, deg, kRight);
    }
    {
        const int type = std::clamp(G("eye_type"), 0, 61);
        const float s = dim(G("eye_scale")), a = 0.12f * static_cast<float>(G("eye_aspect")) + 0.64f;
        const float dx = sx * static_cast<float>(G("eye_x")), y = 18.451525f + magic * sy * static_cast<float>(G("eye_y"));
        const float w = 342.0f / 64.0f * s, h = 288.0f / 64.0f * s * a, deg = angle(G("eye_rotate") + 32 - kEyeRotate[type]);
        const QImage img = Modulate(LoadTexture(kTexEye, type), kLayered, black, white, Common(G("eye_color")));
        part(img, 32.0f + dx, y, w, h, 360.0f - deg, kLeft), part(img, 32.0f - dx, y, w, h, deg, kRight);
    }
    if (G("mole_type")) {
        const float s = dim(G("mole_scale"));
        part(Modulate(LoadTexture(kTexMole, 1), kAlpha, black), 17.766165f + 2.0f * sx * static_cast<float>(G("mole_x")),
             17.95986f + magic * sy * static_cast<float>(G("mole_y")), s, s, 0, kCentre);
    }
    p.end();
    return mask.convertToFormat(QImage::Format_RGBA8888);
}

QImage Renderer::FaceTexture(const StoreData& mii, int size) const {
    QImage face(size / 2, size, QImage::Format_ARGB32_Premultiplied);
    const unsigned char* skin = kFacelineColor[std::clamp(mii.Get("faceline_color"), 0, 9)];
    face.fill(QColor(skin[0], skin[1], skin[2]));
    QPainter p(&face);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    const auto layer = [&](const QImage& img) {
        if (!img.isNull()) p.drawImage(QRectF(0, 0, face.width(), face.height()), img);
    };
    layer(LoadTexture(kTexMakeup, mii.Get("faceline_makeup")));
    layer(Modulate(LoadTexture(kTexWrinkle, mii.Get("faceline_wrinkle")), kAlpha, Rgb{0, 0, 0}));
    if (const int beard = mii.Get("beard_type"); beard >= 4) layer(Modulate(LoadTexture(kTexBeard, beard - 4), kAlpha, Common(mii.Get("beard_color"))));
    p.end();
    return face.convertToFormat(QImage::Format_RGBA8888);
}

QImage Renderer::Render(const StoreData& mii, int width, int height) const {
    if (!loaded || width <= 0 || height <= 0) return {};
    const int ss = 2;
    Target t(width * ss, height * ss);
    const auto G = [&](const char* key) { return mii.Get(key); };
    const int face = std::clamp(G("faceline_type"), 0, kFaceShapes - 1), hair = std::clamp(G("hair_type"), 0, kHairShapes - 1);
    const int nose = std::clamp(G("nose_type"), 0, 17);
    const Rgb skin = Colour(kFacelineColor[std::clamp(G("faceline_color"), 0, 9)]);
    const Rgb favourite = Colour(kFavoriteColor[std::clamp(G("favorite_color"), 0, 11)]);

    QMatrix4x4 proj, view;
    proj.perspective(15.0f, static_cast<float>(width) / static_cast<float>(height), 100.0f, 2000.0f);
    // Framed like a Mii portrait: the head fills most of the picture, the shoulders just show below it.
    const float look_y = 27.0f, distance = 455.0f * std::max(1.0f, 0.8f * static_cast<float>(height) / static_cast<float>(width));
    view.lookAt(QVector3D(0, look_y, distance), QVector3D(0, look_y, 0), QVector3D(0, 1, 0));
    const QMatrix4x4 vp = proj * view;
    const auto place = [](const std::array<float, 3>& at, float scale = 1.0f, bool flip = false, float dy = 0.0f, float dz = 0.0f) {
        QMatrix4x4 m;
        m.translate(at[0], at[1] + dy, at[2] + dz);
        m.scale(flip ? -scale : scale, scale, scale);
        return m;
    };
    const std::array<float, 3> origin{0, 0, 0};
    const QMatrix4x4 identity;

    // Shoulders: not part of the firmware's Mii data (games bring their own bodies), so a plain small body in the favourite
    // colour, a little wider with the Mii's build. A Mii has no visible neck: the head sits on the body.
    const float build = static_cast<float>(G("build")) / 127.0f;
    Draw(t, Ellipsoid(0, -36.0f, -3.0f, 19.0f + 6.0f * build, 34.0f, 12.0f + 4.0f * build), identity, vp, Material{favourite});

    const QImage face_tex = FaceTexture(mii, 512), mask_tex = MaskTexture(mii, 512);
    Material face_mat{skin};
    face_mat.texture = &face_tex, face_mat.mirror = true;
    Draw(t, LoadMesh(kFaceLine, face), identity, vp, face_mat);
    const bool flip = G("hair_flip") != 0;
    const QMatrix4x4 hair_at = place(hair_translate[static_cast<size_t>(face)], 1.0f, flip);
    Draw(t, LoadMesh(kForeheadNormal, hair), hair_at, vp, Material{skin});
    // Fourteen styles wear a cap or a headband over the top of the head, in the Mii's favourite colour (without it the top
    // of the head was simply missing).
    Draw(t, LoadMesh(kHatNormal, hair), hair_at, vp, Material{favourite});
    Draw(t, LoadMesh(kHairNormal, hair), hair_at, vp, Material{Common(G("hair_color"))});
    const float nose_scale = 0.4f + 0.175f * static_cast<float>(G("nose_scale")), nose_dy = -1.5f * static_cast<float>(G("nose_y") - 8);
    const QMatrix4x4 nose_at = place(nose_translate[static_cast<size_t>(face)], nose_scale, false, nose_dy);
    Draw(t, LoadMesh(kNose, nose), nose_at, vp, Material{skin});
    if (const int beard = G("beard_type"); beard >= 1 && beard <= 3)
        Draw(t, LoadMesh(kBeard, beard), place(beard_translate[static_cast<size_t>(face)]), vp, Material{Common(G("beard_color"))});

    // Transparent layers, back to front: the face parts, the nose line, the glasses.
    Material mask_mat;
    mask_mat.texture = &mask_tex, mask_mat.blend = true;
    Draw(t, LoadMesh(kMask, face), identity, vp, mask_mat);
    const QImage line_tex = Modulate(LoadTexture(kTexNoseLine, nose), kAlpha, Rgb{0.1f, 0.05f, 0.04f});
    Material line_mat;
    line_mat.texture = &line_tex, line_mat.blend = true;
    Draw(t, LoadMesh(kNoseLine, nose), nose_at, vp, line_mat);
    if (const int glasses = G("glasses_type"); glasses > 0) {
        const QImage glass_tex = Modulate(LoadTexture(kTexGlass, glasses), kLuminanceAlpha, Common(G("glasses_color")));
        Material glass_mat;
        glass_mat.texture = &glass_tex, glass_mat.blend = true, glass_mat.mirror = true;
        const auto& nt = nose_translate[static_cast<size_t>(face)];
        const std::array<float, 3> at{0, nt[1] + 5.0f - 1.5f * static_cast<float>(G("glasses_y") - 11), nt[2] + 2.0f};
        Draw(t, LoadMesh(kGlasses, 0), place(at, 0.4f + 0.15f * static_cast<float>(G("glasses_scale"))), vp, glass_mat);
    }
    (void)origin;

    QImage big(t.w, t.h, QImage::Format_RGBA8888);
    for (int y = 0; y < t.h; ++y) {
        uchar* d = big.scanLine(y);
        const float* s = &t.rgba[static_cast<size_t>(y) * t.w * 4];
        for (int x = 0; x < t.w * 4; ++x) d[x] = static_cast<uchar>(std::clamp(s[x], 0.0f, 1.0f) * 255.0f + 0.5f);
    }
    return big.scaled(width, height, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
}

} // namespace Mii
