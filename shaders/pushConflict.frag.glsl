#version 450 core

// The other half of the disagreement: `tint` is a vec2 here and a vec4 in the vertex stage.
layout(push_constant) uniform Push {
    vec2 tint;
} push;

layout(location = 0) out vec4 outColor;

void main() {
    outColor = vec4(push.tint, 0.0, 1.0);
}
