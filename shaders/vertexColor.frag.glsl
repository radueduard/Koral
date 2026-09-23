#version 450 core

// Writes the interpolated vertex colour straight out, so a readback shows what the vertex stage
// actually fetched.
layout(location = 0) in vec3 fragColor;
layout(location = 0) out vec4 outColor;

void main() {
    outColor = vec4(fragColor, 1.0);
}
