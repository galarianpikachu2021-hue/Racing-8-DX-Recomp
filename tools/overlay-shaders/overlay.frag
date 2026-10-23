#version 450
layout(set = 0, binding = 0) uniform sampler2D atlas;
layout(push_constant) uniform P {
    layout(offset = 8) float hdr_paper; // 0 = SDR output; otherwise nits of SDR white for an HDR10 (PQ, Rec.2020) swapchain
} p;
layout(location = 0) in vec2 uv;
layout(location = 1) in vec4 color;
layout(location = 0) out vec4 out_color;
vec3 srgb_to_linear(vec3 c) {
    return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), step(vec3(0.04045), c));
}
vec3 pq(vec3 nits) {
    vec3 y = pow(clamp(nits / 10000.0, 0.0, 1.0), vec3(0.1593017578125));
    return pow((0.8359375 + 18.8515625 * y) / (1.0 + 18.6875 * y), vec3(78.84375));
}
void main() {
    vec3 rgb = color.rgb;
    if (p.hdr_paper > 0.0) {
        const mat3 to2020 = mat3(0.627404, 0.069097, 0.016391,
                                 0.329283, 0.919540, 0.088013,
                                 0.043313, 0.011362, 0.895595);
        rgb = pq(to2020 * srgb_to_linear(rgb) * p.hdr_paper);
    }
    out_color = vec4(rgb, color.a * texture(atlas, uv).r);
}
