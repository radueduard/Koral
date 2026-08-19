#version 450 core

// The two classic padding traps, in one block.
//
// A mat3 reserves three columns of four floats — 48 bytes for nine values — and an array of vec3
// strides 16 bytes per element for 12 bytes of data. C++ packs both tight, so copying either one
// over verbatim puts every column but the first in the wrong place. Reading every column and every
// element here is what makes a mis-laid write show up in the readback.
layout(push_constant) uniform Push {
    mat3 basis;
    vec3 tints[3];
} push;

layout(location = 0) out vec4 outColor;

void main() {
    const vec3 fromMatrix = push.basis[0] + push.basis[1] + push.basis[2];
    const vec3 fromArray  = push.tints[0] + push.tints[1] + push.tints[2];
    outColor = vec4(fromMatrix + fromArray, 1.0);
}
