#version 460
// SPDX-License-Identifier: GPL-3.0-or-later
// Per-object motion vectors (kart-recomp, docs/UPSCALER.md "Per-object motion vectors"). Runs with the game's own vertex shader
// in a replay of the scene's opaque draws; that vertex shader's clip positions are captured (transform feedback, 3 per
// triangle) every frame. Here a visible fragment finds its triangle in LAST frame's capture of the same draw, interpolates the
// three previous clip positions with this fragment's barycentrics (perspective-correct: the same weights a varying gets) and
// writes prev - cur in render pixels into the motion image, plus 0 into the mismatch mask (the vector is exact).
#extension GL_EXT_fragment_shader_barycentric : require

layout(set = 1, binding = 0, std430) readonly buffer Prev { vec4 pos[]; } prev;
layout(set = 1, binding = 1) uniform sampler2D scene_depth;
layout(set = 1, binding = 2, rg16f) uniform writeonly image2D motion;
layout(set = 1, binding = 3, r8) uniform writeonly image2D mismatch;
layout(set = 1, binding = 4, rgb10_a2) uniform writeonly image2D debug_vis;

// Offset 48: the game's vertex shader uses the first 48 bytes (rescaling and render-area words).
layout(push_constant) uniform Push {
    layout(offset = 48) vec4 viewport; // x, y, width, height (height negative: y flipped), unjittered, render pixels
    vec2 jitter;                       // this frame's viewport shift
    vec2 depth_scale;                  // scene depth texels per motion texel
    uint base;                         // first vec4 of this draw's triangles in last frame's capture
    uint valid;                        // 0: no capture of this draw last frame (only this frame's capture runs)
    uint debug;                        // 1: paint vectors into debug_vis
    float tolerance;                   // depth match
} pc;

void main() {
    if (pc.valid == 0u) discard;
    const ivec2 pix = ivec2(gl_FragCoord.xy);
    const float scene = texelFetch(scene_depth, ivec2(vec2(pix) * pc.depth_scale), 0).r;
    // Only the surface the scene kept: the same vertex shader and state give the same depth (absolute: the game's depth is
    // 1 - near/distance, so far surfaces sit close together near 1 and a relative test would merge them).
    if (abs(gl_FragCoord.z - scene) > pc.tolerance) discard;
    const uint i = pc.base + uint(gl_PrimitiveID) * 3u;
    const vec4 c = prev.pos[i] * gl_BaryCoordEXT.x + prev.pos[i + 1u] * gl_BaryCoordEXT.y + prev.pos[i + 2u] * gl_BaryCoordEXT.z;
    if (!(c.w > 1e-5)) discard; // behind the camera last frame: the camera layer keeps this pixel
    const vec2 ndc = c.xy / c.w;
    const vec2 prev_px = pc.viewport.xy + (ndc * 0.5 + 0.5) * pc.viewport.zw;
    const vec2 cur_px = gl_FragCoord.xy - pc.jitter;
    const vec2 mv = prev_px - cur_px;
    if (any(isnan(mv)) || any(greaterThan(abs(mv), vec2(4096.0)))) discard;
    imageStore(motion, pix, vec4(mv, 0.0, 0.0));
    imageStore(mismatch, pix, vec4(0.0));
    if (pc.debug != 0u) {
        const vec2 v = clamp(mv / 8.0, vec2(-1.0), vec2(1.0));
        imageStore(debug_vis, pix, vec4(0.5 + 0.5 * v.x, 0.5 + 0.5 * v.y, 1.0, 1.0)); // blue tint: object layer
    }
}
