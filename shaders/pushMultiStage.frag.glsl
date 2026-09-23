#version 450 core

// The other half: reads `color` out of the same shared block the vertex stage takes `offset` from.
#include "pushMultiStage.h.glsl"

layout(location = 0) out vec4 outColor;

void main() {
    outColor = push.color;
}
