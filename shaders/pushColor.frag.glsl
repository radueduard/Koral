#version 450 core

// Emits the color supplied via push constants, so tests can verify the
// push-constant upload path end-to-end (native ranges on Vulkan, the emulated
// std140 UBO on OpenGL) by asserting the exact readback value.
layout(location = 0) out vec4 outColor;

layout(push_constant) uniform PushColor {
    vec4 color;
} pc;

void main() {
    outColor = pc.color;
}
