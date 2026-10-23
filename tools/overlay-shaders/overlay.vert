#version 450
// Overlay menu: pixel-space quads with a font-atlas UV and a packed colour.
layout(location = 0) in vec2 in_pos;
layout(location = 1) in vec2 in_uv;
layout(location = 2) in vec4 in_color;
layout(push_constant) uniform Push { vec2 screen; } pc;
layout(location = 0) out vec2 uv;
layout(location = 1) out vec4 color;
void main() {
    gl_Position = vec4(in_pos / pc.screen * 2.0 - 1.0, 0.0, 1.0);
    uv = in_uv;
    color = in_color;
}
