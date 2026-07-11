#version 450 core

// Emits a constant color so the offscreen-render test can assert an exact RGBA8
// value at every covered texel.
layout(location = 0) out vec4 outColor;

void main() {
    outColor = vec4(0.0, 1.0, 0.0, 1.0); // solid green
}
