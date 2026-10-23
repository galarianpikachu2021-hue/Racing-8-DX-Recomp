#version 450
// HDR10 (PQ, Rec.2020) present pass. The game's picture is SDR, sRGB-encoded. The 3D image gets its highlights
// expanded towards the display's peak; the HUD (found by comparing against a copy taken before it was drawn)
// stays at paper white.
layout(set = 0, binding = 0) uniform sampler2D final_img; // the finished frame, HUD included
layout(set = 0, binding = 1) uniform sampler2D clean_img; // the 3D image before the HUD
layout(push_constant) uniform P {
    vec4 rect;       // destination rectangle in pixels: x, y, w, h
    float paper;     // nits of SDR white
    float peak;      // nits of the brightest highlight
    float mask_ok;   // 1: clean_img is this frame's HUD-less copy; 2: clean_img.r is the HUD amount; 0: no mask
    float expand;    // 0 = plain SDR in HDR10, 1 = full highlight expansion
} p;
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 out_color;

vec3 srgb_to_linear(vec3 c) {
    return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), step(vec3(0.04045), c));
}
vec3 pq(vec3 nits) {
    vec3 y = pow(clamp(nits / 10000.0, 0.0, 1.0), vec3(0.1593017578125));
    return pow((0.8359375 + 18.8515625 * y) / (1.0 + 18.6875 * y), vec3(78.84375));
}
void main() {
    vec2 px = gl_FragCoord.xy;
    vec2 t = (px - p.rect.xy) / p.rect.zw;
    if (t.x < 0.0 || t.y < 0.0 || t.x >= 1.0 || t.y >= 1.0) {
        out_color = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }
    vec3 c = texture(final_img, t).rgb;
    vec3 lin = srgb_to_linear(c);
    // Deeper picture, applied to every pixel (so nothing changes between 3D and menu frames): a steeper curve
    // (darker mid-tones) and more saturation. White stays white, so the HUD is unaffected.
    lin = pow(max(lin, vec3(0.0)), vec3(1.25));
    float yl = dot(lin, vec3(0.2126, 0.7152, 0.0722));
    lin = max(mix(vec3(yl), lin, 1.12), vec3(0.0));
    float scale = 1.0;
    if (p.mask_ok > 0.5) {
        vec3 d = abs(c - texture(clean_img, t).rgb);
        float ui = p.mask_ok > 1.5 ? texture(clean_img, t).r : smoothstep(0.004, 0.02, max(d.r, max(d.g, d.b)));
        float y = max(lin.r, max(lin.g, lin.b));
        // Only the very top of the range stretches, and gently: SDR white objects must not blow out. The ratio
        // is square-rooted and the knee sits at 85% of SDR white.
        float r = sqrt(max(p.peak / p.paper, 1.0));
        float k = smoothstep(0.3, 1.0, y); // a wide, gentle ramp: a narrow knee turns small frame-to-frame changes in bloom into visible flicker
        float boost = 1.0 + (r - 1.0) * p.expand * k * k;
        scale = mix(boost, 1.0, ui);
    }
    vec3 nits = lin * p.paper * scale;
    const mat3 to2020 = mat3(0.627404, 0.069097, 0.016391,
                             0.329283, 0.919540, 0.088013,
                             0.043313, 0.011362, 0.895595);
    out_color = vec4(pq(to2020 * nits), 1.0);
}
