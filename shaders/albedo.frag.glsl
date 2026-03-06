#version 450 core
#extension GL_KHR_vulkan_glsl : enable

layout(location = 0) in vec2 uv;

layout(set = 1, binding = 1) uniform sampler2D albedo;
layout(set = 1, binding = 2) uniform sampler2D normal;

layout(location = 0) out vec4 outColor;

void main() {
    outColor = texture(albedo, uv);
//    outColor = vec4(uv, 0.0, 1.0);
}