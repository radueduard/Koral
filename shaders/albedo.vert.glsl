#version 450 core
#extension GL_KHR_vulkan_glsl : enable
#extension GL_EXT_scalar_block_layout : enable

layout(location = 0) in vec3 position;
layout(location = 2) in vec2 inUV;

layout(set = 0, binding = 0, std430) uniform Camera {
    mat4 view;
    mat4 proj;
} camera;

layout(set = 1, binding = 0) uniform Model {
    mat4 model;
};

layout(location = 0) out vec2 outUV;

void main() {
    outUV = inUV;
    gl_Position = camera.proj * camera.view * model * vec4(position, 1.0);
}