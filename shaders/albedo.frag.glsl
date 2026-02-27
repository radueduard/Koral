#version 450 core

layout(location = 0) in vec2 uv;

layout(binding = 2) uniform sampler2D imageSampler;

layout(location = 0) out vec4 outColor;

void main() {
    outColor = texture(imageSampler, uv);
//    outColor = vec4(uv, 0.0, 1.0);
}