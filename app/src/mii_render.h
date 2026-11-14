// SPDX-License-Identifier: GPL-3.0-or-later
// The launcher's Mii preview: draws a Mii's head and shoulders from the console's own Mii resources (the "MiiModel" system
// archive of the player's firmware: ShapeHigh.dat / ShapeMid.dat with the head meshes, NXTextureMidSRGB.dat with the face
// textures). Nothing from the firmware is shipped: without it there is no preview.
//
// How a Mii is put together (the same steps the console's Mii library takes):
//   1. mask texture: eyes, eyebrows, mouth, mustache and mole are placed on a transparent square by the Mii's position, size and
//      rotation values;
//   2. face texture: skin colour, then makeup, wrinkles and a painted beard;
//   3. the head meshes (face, forehead, hair, nose, nose line, beard, glasses) are drawn with the mask on top of the face.
// The resource layout, the placement formulas and the colour tables follow the MIT/Apache-licensed "vee" Mii library
// (github.com/j0lol/vee); the drawing is a small software rasterizer, so the preview needs no GPU.
#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <vector>

#include <QByteArray>
#include <QImage>
#include <QString>

#include "mii_db.h"

namespace Mii {

class Renderer {
public:
    /// Reads the resource files from `dir`. False (and why) when they are missing or not in the expected layout.
    bool Load(const QString& dir, QString* error = nullptr);
    bool Loaded() const { return loaded; }
    /// The eye, mouth and makeup textures are ASTC, which the engine decodes (`mk8d-engine --astc-decode <in> <out>`): this is
    /// the input file for that, and Load reads the result from `astc.rgba` in the resource folder. Without it those parts are
    /// missing from the picture (NeedsAstc says so).
    QByteArray AstcJob() const;
    bool NeedsAstc() const { return loaded && astc.empty(); }
    /// Head and shoulders of `mii`, `width` x `height` pixels on a transparent background. Null image when nothing is loaded.
    QImage Render(const StoreData& mii, int width, int height) const;

    struct Mesh {
        std::vector<std::array<float, 3>> pos, normal;
        std::vector<std::array<float, 2>> uv;
        std::vector<uint16_t> index;
        bool Empty() const { return index.empty(); }
    };

private:
    struct Element {
        uint32_t offset = 0, size = 0, packed = 0;
        // shapes
        uint32_t attr_offset[5]{}, attr_size[5]{}, index_offset = 0, index_size = 0;
        // textures
        uint16_t width = 0, height = 0;
        uint8_t format = 0, tile_mode = 0;
    };
    Mesh LoadMesh(int kind, int index) const;
    QImage LoadTexture(int kind, int index) const; ///< RGBA8888, as stored (channels mean what the part says)
    QImage MaskTexture(const StoreData& mii, int size) const;
    QImage FaceTexture(const StoreData& mii, int size) const;

    bool loaded = false;
    QByteArray shape_file, texture_file;
    std::vector<Element> shapes[12], textures[11];
    std::vector<std::array<float, 3>> hair_translate, nose_translate, beard_translate; ///< per face shape
    std::map<int, QImage> astc; ///< decoded ASTC textures by kind * 1000 + index
    mutable std::map<int, Mesh> mesh_cache;
    mutable std::map<int, QImage> texture_cache;
};

} // namespace Mii
