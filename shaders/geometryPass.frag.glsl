#version 450 core

layout(location = 0) in vec4 worldPosition;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec3 tangent;
layout(location = 3) in vec3 bitangent;
layout(location = 4) in vec2 uv;

layout(set = 1, binding = 1) uniform sampler2D imageSampler;
layout(set = 1, binding = 2) uniform sampler2D normalSampler;
layout(set = 1, binding = 3) uniform sampler2D metallicRoughnessSampler;
layout(set = 1, binding = 4) uniform sampler2D aoSampler;
layout(set = 1, binding = 5) uniform sampler2D emissiveSampler;

layout(location = 0) out vec3 outPosition;
layout(location = 1) out vec4 outColor;
layout(location = 2) out vec3 outNormal;
layout(location = 3) out vec3 outMetallicRoughnessAO;
layout(location = 4) out vec3 outEmissive;

vec3 getNormalFromMap() {
    vec3 normalColor = texture(normalSampler, uv).rgb;
    normalColor = normalColor * 2.f - 1.f;
    mat3 TBN = mat3(tangent, bitangent, normal);
    return normalize(TBN * normalColor);
}

void main() {
    outPosition = worldPosition.xyz / worldPosition.w;
    outColor = texture(imageSampler, uv);
    outNormal = getNormalFromMap() * 0.5f + 0.5f; // Map from [-1, 1] to [0, 1]
    vec2 metallicRoughness = texture(metallicRoughnessSampler, uv).bg;
    float ao = texture(aoSampler, uv).r;
    outMetallicRoughnessAO = vec3(metallicRoughness, ao);
    outEmissive = texture(emissiveSampler, uv).rgb;
}