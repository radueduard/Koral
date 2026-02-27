#version 450 core

layout(location = 0) in vec4 worldPosition;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec2 uv;

layout(binding = 2) uniform sampler2D imageSampler;

layout(location = 0) out vec3 outPosition;
layout(location = 1) out vec4 outColor;
layout(location = 2) out vec3 outNormal;

void main() {
    outPosition = worldPosition.xyz / worldPosition.w;
    outColor = texture(imageSampler, uv);
    outNormal = (normal + 1.f) / 2.f;
}