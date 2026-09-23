#version 450 core

// Reads an interleaved position+colour vertex described by a hand-written kor::VertexLayout.
// The inputs are matched to that layout by *semantic* rather than by location, so the offsets the
// layout declares are what decide which bytes each one is fed — which is what the mesh-builder
// test checks by giving every vertex the same colour and asserting the exact texel value.
#pragma vertex(POSITION)
layout(location = 0) in vec3 inPosition;
#pragma vertex(COLOR)
layout(location = 1) in vec3 inColor;

layout(location = 0) out vec3 fragColor;

void main() {
    gl_Position = vec4(inPosition, 1.0);
    fragColor = inColor;
}
