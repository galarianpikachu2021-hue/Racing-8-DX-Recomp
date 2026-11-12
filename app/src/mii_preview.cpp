// SPDX-License-Identifier: GPL-3.0-or-later
#include "mii_preview.h"

#include <QPainter>
#include <QPainterPath>
#include <algorithm>
#include <array>
#include <cmath>

namespace {
// Classic Mii colours (Wii U / 3DS) and where the Switch's 100 "common colours" keep them: hair 0-7 at common 8,1..7, eye colours at
// 8..13, mouth colours at 19..23, glasses colours at 8,14..18,0. Every other common colour is shown as its nearest classic colour
// (the console's own mapping, FromVer3*ColorTable), so the shade is close but not exact.
constexpr std::array<QRgb, 8> kHair = {qRgb(30, 26, 24),  qRgb(64, 32, 16),  qRgb(92, 24, 10),  qRgb(124, 58, 20),
                                       qRgb(120, 120, 128), qRgb(78, 62, 16), qRgb(136, 88, 24), qRgb(208, 160, 74)};
constexpr std::array<QRgb, 6> kEye = {qRgb(0, 0, 0),     qRgb(108, 112, 112), qRgb(102, 60, 44),
                                      qRgb(96, 94, 48),  qRgb(70, 84, 168),   qRgb(56, 112, 88)};
constexpr std::array<QRgb, 5> kMouth = {qRgb(216, 82, 8), qRgb(240, 12, 8), qRgb(245, 72, 72), qRgb(240, 154, 116), qRgb(140, 80, 64)};
constexpr std::array<QRgb, 6> kGlass = {qRgb(0, 0, 0), qRgb(96, 56, 16), qRgb(152, 0, 0), qRgb(32, 48, 96), qRgb(144, 88, 0), qRgb(96, 96, 88)};
constexpr std::array<QRgb, 10> kSkin = {qRgb(255, 211, 173), qRgb(255, 182, 107), qRgb(222, 121, 66), qRgb(255, 170, 140), qRgb(173, 81, 41),
                                        qRgb(99, 44, 24),    qRgb(255, 222, 190), qRgb(246, 172, 120), qRgb(140, 64, 32),  qRgb(70, 34, 20)};
constexpr std::array<QRgb, 12> kFavorite = {qRgb(210, 30, 20),  qRgb(255, 110, 25), qRgb(255, 216, 32), qRgb(120, 210, 32),
                                            qRgb(0, 120, 48),   qRgb(10, 72, 180),  qRgb(60, 170, 222), qRgb(245, 90, 125),
                                            qRgb(115, 40, 173), qRgb(72, 56, 24),   qRgb(224, 224, 224), qRgb(24, 24, 20)};
// FromVer3HairColorTable / EyeColor / MouthlineColor / GlassColor (Ryujinx/suyu raw_data.cpp): common colour -> classic index.
constexpr std::array<uint8_t, 100> kToHair = {
    0, 1, 2, 3, 4, 5, 6, 7, 0, 4, 3, 5, 4, 4, 6, 2, 0, 6, 4, 3, 2, 2, 7, 3, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 4, 4, 4, 4, 4, 4, 4, 0, 0,
    4, 4, 4, 4, 4, 4, 0, 0, 0, 4, 4, 4, 4, 4, 4, 5, 5, 5, 4, 4, 4, 4, 4, 4, 4, 5, 7, 5, 7, 7, 7, 7, 7, 6, 7, 7, 7, 7, 7, 3, 7, 7, 7, 7, 7, 0, 4, 4, 4, 4};
constexpr std::array<uint8_t, 100> kToEye = {
    0, 2, 2, 2, 1, 3, 2, 3, 0, 1, 2, 3, 4, 5, 2, 2, 4, 2, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 0, 0, 4, 4, 4, 4, 4, 4, 4, 1, 0, 4,
    4, 4, 4, 4, 4, 4, 0, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 3, 3, 3, 3, 3, 3, 3, 3, 2, 2, 3, 3, 3, 3, 2, 2, 2, 2, 2, 1, 1, 1, 1, 1, 1};
constexpr std::array<uint8_t, 100> kToMouth = {
    4, 4, 4, 4, 4, 4, 4, 3, 4, 4, 4, 4, 4, 4, 4, 1, 4, 4, 4, 0, 1, 2, 3, 4, 4, 2, 3, 3, 4, 4, 4, 4, 1, 4, 4, 2, 3, 3, 4, 4, 4, 4, 4, 4, 4, 3, 3, 3, 4, 4,
    4, 3, 3, 3, 3, 3, 4, 4, 4, 4, 4, 3, 3, 3, 3, 4, 4, 4, 4, 3, 3, 3, 3, 3, 3, 4, 4, 3, 3, 3, 3, 3, 3, 4, 3, 3, 3, 3, 3, 4, 0, 3, 3, 3, 3, 4, 3, 3, 3, 3};
constexpr std::array<uint8_t, 100> kToGlass = {
    0, 1, 1, 1, 5, 1, 1, 4, 0, 5, 1, 1, 3, 5, 1, 2, 3, 4, 5, 4, 2, 2, 4, 4, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
    3, 3, 3, 3, 3, 3, 0, 0, 0, 5, 5, 5, 5, 5, 5, 0, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 1, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 5, 5, 5, 5, 5, 5};

QColor Common(int index, const std::array<uint8_t, 100>& to, const QRgb* classic) { return QColor(classic[to[static_cast<size_t>(std::clamp(index, 0, 99))]]); }
QColor HairColor(int c) {
    // Exact where the console keeps a classic hair colour (common 8 = black, 1..7 = the others).
    if (c == 8) return QColor(kHair[0]);
    if (c >= 1 && c <= 7) return QColor(kHair[static_cast<size_t>(c)]);
    return Common(c, kToHair, kHair.data());
}
QColor EyeColor(int c) { return c >= 8 && c <= 13 ? QColor(kEye[static_cast<size_t>(c - 8)]) : Common(c, kToEye, kEye.data()); }
QColor MouthColor(int c) { return c >= 19 && c <= 23 ? QColor(kMouth[static_cast<size_t>(c - 19)]) : Common(c, kToMouth, kMouth.data()); }
QColor GlassColor(int c) {
    if (c == 8 || c == 0) return QColor(kGlass[0]);
    if (c >= 14 && c <= 18) return QColor(kGlass[static_cast<size_t>(c - 13)]);
    return Common(c, kToGlass, kGlass.data());
}
double Scale(int s) { return 0.6 + 0.1 * s; } // part sizes 0..8, 4 = normal
} // namespace

MiiPreview::MiiPreview(QWidget* parent) : QWidget(parent) {
    setMinimumSize(260, 340);
}

void MiiPreview::setValues(const std::map<std::string, int>& vals) {
    values = vals;
    update();
}

int MiiPreview::v(const char* key, int fallback) const {
    const auto it = values.find(key);
    return it == values.end() ? fallback : it->second;
}

void MiiPreview::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), palette().base());
    // Layout in a 256 x 300 design space, scaled to the widget.
    const double k = std::min(width() / 256.0, (height() - 44) / 300.0);
    p.translate((width() - 256 * k) / 2, 4);
    p.scale(k, k);

    const QColor skin(kSkin[static_cast<size_t>(std::clamp(v("faceline_color"), 0, 9))]);
    const QColor hair = HairColor(v("hair_color", 1));
    const bool flip = v("hair_flip") != 0;

    // Shoulders in the favourite colour.
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(kFavorite[static_cast<size_t>(std::clamp(v("favorite_color"), 0, 11))]));
    p.drawRoundedRect(QRectF(48, 252, 160, 48), 24, 24);

    // Face shape: 12 types from round to long/square (width, height, corner roundness).
    static const double kShape[12][3] = {{1.00, 1.00, 1.0}, {0.96, 1.04, 0.8}, {1.04, 0.96, 1.0}, {0.92, 1.08, 0.7}, {1.02, 1.00, 0.6}, {0.98, 1.06, 0.9},
                                         {1.06, 0.98, 0.7}, {0.94, 1.00, 1.0}, {1.00, 1.08, 0.6}, {1.08, 1.02, 0.8}, {0.96, 0.98, 0.5}, {1.02, 1.04, 0.9}};
    const auto& sh = kShape[std::clamp(v("faceline_type"), 0, 11)];
    const QRectF face(128 - 78 * sh[0], 140 - 96 * sh[1], 156 * sh[0], 192 * sh[1]);

    // Hair behind the face, then the face, then the fringe on top. The length varies with the style number (a hint only: the console
    // has 132 styles of its own).
    p.setBrush(hair);
    const double length = 0.62 + 0.12 * (v("hair_type") % 4);
    p.drawRoundedRect(QRectF(face.left() - 12, face.top() - 16, face.width() + 24, face.height() * length + 16), 70, 70);
    p.setBrush(skin);
    p.drawRoundedRect(face, face.width() / 2 * sh[2], face.height() / 2 * sh[2]);
    // ears
    p.drawEllipse(QRectF(face.left() - 10, 128, 20, 30));
    p.drawEllipse(QRectF(face.right() - 10, 128, 20, 30));
    {
        QPainterPath fringe;
        const double top = face.top() - 8, mid = face.top() + 38 + (v("hair_type") % 3) * 6;
        fringe.moveTo(face.left() - 4, face.top() + 70);
        fringe.cubicTo(face.left() - 6, top, face.right() + 6, top, face.right() + 4, face.top() + 70);
        const double part = flip ? face.left() + face.width() * 0.35 : face.right() - face.width() * 0.35;
        fringe.cubicTo(face.right() - 10, mid, part, mid - 12, part, face.top() + 14);
        fringe.cubicTo(part, mid - 12, face.left() + 10, mid, face.left() - 4, face.top() + 70);
        p.setBrush(hair);
        p.drawPath(fringe);
    }

    // Makeup (blush) and wrinkles, when set.
    if (v("faceline_makeup") > 0) {
        p.setBrush(QColor(240, 120, 120, 90));
        p.drawEllipse(QPointF(128 - 46, 160), 16, 10);
        p.drawEllipse(QPointF(128 + 46, 160), 16, 10);
    }
    if (v("faceline_wrinkle") > 0) {
        p.setPen(QPen(skin.darker(140), 1.5));
        p.drawArc(QRectF(128 - 64, 150, 22, 22), 200 * 16, 60 * 16);
        p.drawArc(QRectF(128 + 42, 150, 22, 22), 280 * 16, 60 * 16);
        p.setPen(Qt::NoPen);
    }

    // Eyebrows.
    const QColor brow = HairColor(v("eyebrow_color", 1));
    const double by = 70 + v("eyebrow_y", 7) * 4.0, bx = 24 + v("eyebrow_x", 2) * 4.0;
    const double bs = Scale(v("eyebrow_scale", 4)), ba = 0.7 + 0.1 * v("eyebrow_aspect", 3), brot = (v("eyebrow_rotate", 6) - 6) * 5.0;
    for (const int side : {-1, 1}) {
        p.save();
        p.translate(128 + side * (bx + 12), by);
        p.rotate(-side * brot);
        p.setPen(QPen(brow, 5 * bs * ba, Qt::SolidLine, Qt::RoundCap));
        p.drawArc(QRectF(-14 * bs, -4, 28 * bs, 12), 30 * 16, 120 * 16);
        p.restore();
    }
    p.setPen(Qt::NoPen);

    // Eyes.
    const double ey = 60 + v("eye_y", 12) * 5.0, ex = 22 + v("eye_x", 2) * 4.0;
    const double es = Scale(v("eye_scale", 4)), eh = 0.7 + 0.1 * v("eye_aspect", 3), erot = (v("eye_rotate", 4) - 4) * 5.0;
    for (const int side : {-1, 1}) {
        p.save();
        p.translate(128 + side * (ex + 10), ey);
        p.rotate(-side * erot);
        p.setBrush(Qt::white);
        p.setPen(QPen(QColor(40, 30, 30), 1.6));
        p.drawEllipse(QPointF(0, 0), 11 * es, 9 * es * eh);
        p.setPen(Qt::NoPen);
        p.setBrush(EyeColor(v("eye_color", 8)));
        p.drawEllipse(QPointF(0, 0), 6 * es, 7 * es * eh);
        p.setBrush(QColor(15, 10, 10));
        p.drawEllipse(QPointF(0, 0), 3 * es, 3.5 * es * eh);
        p.setBrush(Qt::white);
        p.drawEllipse(QPointF(-2 * es, -2.5 * es * eh), 1.4 * es, 1.4 * es);
        p.restore();
    }

    // Glasses.
    if (v("glasses_type") > 0) {
        const double gy = 75 + v("glasses_y", 10) * 4.5, gs = Scale(v("glasses_scale", 4));
        p.setPen(QPen(GlassColor(v("glasses_color", 8)), 3));
        p.setBrush(Qt::NoBrush);
        const bool round = v("glasses_type") % 2 == 0;
        for (const int side : {-1, 1}) {
            const QRectF lens(128 + side * (ex + 10) - 17 * gs, gy - 13 * gs, 34 * gs, 26 * gs);
            round ? p.drawEllipse(lens) : p.drawRoundedRect(lens, 5, 5);
        }
        p.drawLine(QPointF(128 - (ex + 10) + 17 * gs, gy - 2), QPointF(128 + (ex + 10) - 17 * gs, gy - 2));
        p.setPen(Qt::NoPen);
    }

    // Nose.
    const double ny = 100 + v("nose_y", 9) * 4.0, ns = Scale(v("nose_scale", 4));
    p.setPen(QPen(skin.darker(150), 2.2, Qt::SolidLine, Qt::RoundCap));
    p.setBrush(Qt::NoBrush);
    p.drawArc(QRectF(128 - 9 * ns, ny - 6 * ns, 18 * ns, 12 * ns), 200 * 16, 140 * 16);
    p.setPen(Qt::NoPen);

    const double my = 105 + v("mouth_y", 13) * 4.5;
    const QColor beard = HairColor(v("beard_color", 8));
    // Beard (under the mouth and mustache).
    if (v("beard_type") > 0) {
        QColor b = beard;
        b.setAlpha(v("beard_type") >= 4 ? 230 : 140); // 1-3 stubble-like, 4-5 full
        p.setBrush(b);
        QPainterPath s;
        s.moveTo(face.left() + 8, 160);
        s.cubicTo(face.left() + 10, face.bottom() + 6, face.right() - 10, face.bottom() + 6, face.right() - 8, 160);
        s.cubicTo(face.right() - 30, my - 8, face.left() + 30, my - 8, face.left() + 8, 160);
        p.drawPath(s);
    }


    // Mouth.
    const double ms = Scale(v("mouth_scale", 4)), ma = 0.7 + 0.1 * v("mouth_aspect", 3);
    {
        QPainterPath m;
        const double w = 22 * ms, h = 9 * ms * ma;
        m.moveTo(128 - w, my);
        m.cubicTo(128 - w / 2, my + h * 1.6, 128 + w / 2, my + h * 1.6, 128 + w, my);
        m.cubicTo(128 + w / 2, my + h * 0.4, 128 - w / 2, my + h * 0.4, 128 - w, my);
        p.setBrush(MouthColor(v("mouth_color", 19)));
        p.drawPath(m);
    }

    // Mustache.
    if (v("mustache_type") > 0) {
        const double sy = 100 + v("mustache_y", 10) * 4.5, ss = Scale(v("mustache_scale", 4));
        p.setBrush(beard);
        QPainterPath s;
        s.moveTo(128, sy - 3);
        s.cubicTo(128 - 14 * ss, sy - 8, 128 - 26 * ss, sy, 128 - 24 * ss, sy + 6);
        s.cubicTo(128 - 14 * ss, sy + 2, 128 - 6, sy + 4, 128, sy + 2);
        s.cubicTo(128 + 6, sy + 4, 128 + 14 * ss, sy + 2, 128 + 24 * ss, sy + 6);
        s.cubicTo(128 + 26 * ss, sy, 128 + 14 * ss, sy - 8, 128, sy - 3);
        p.drawPath(s);
    }
    // Mole.
    if (v("mole_type") > 0) {
        const double s = Scale(v("mole_scale", 4));
        p.setBrush(QColor(40, 26, 20));
        p.drawEllipse(QPointF(70 + v("mole_x", 2) * 7.0, 50 + v("mole_y", 20) * 5.0), 2.6 * s, 2.6 * s);
    }

    // The part numbers, since the console draws each style from its own artwork.
    p.resetTransform();
    p.setPen(palette().text().color());
    QFont f = font();
    f.setPointSizeF(f.pointSizeF() * 0.85);
    p.setFont(f);
    const QString parts = tr("Hair %1  Eyes %2  Brows %3  Nose %4  Mouth %5").arg(v("hair_type")).arg(v("eye_type")).arg(v("eyebrow_type"))
                              .arg(v("nose_type")).arg(v("mouth_type"));
    p.drawText(QRectF(0, height() - 40, width(), 18), Qt::AlignCenter, parts);
    p.setPen(palette().placeholderText().color());
    p.drawText(QRectF(0, height() - 22, width(), 18), Qt::AlignCenter, tr("Sketch; the game draws each style"));
}
