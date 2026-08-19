#version 450 core

// Deliberately std140: an array of floats strides 16 bytes here instead of 4, so a C++ array
// written into it lands one element in four. Used to check that the engine notices.
layout(push_constant, std140) uniform Push {
    float weights[4];
} push;

layout(location = 0) out vec4 outColor;

void main() {
    outColor = vec4(push.weights[0], push.weights[1], push.weights[2], push.weights[3]);
}
