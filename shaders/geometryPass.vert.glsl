#version 450 core

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec2 inUV;

layout(binding = 0) uniform Camera {
    mat4 view;
    mat4 proj;
} camera;

layout(binding = 1) uniform Model {
    mat4 data;
} model;

layout(location = 0) out vec4 worldPosition;
layout(location = 1) out vec3 outNormal;
layout(location = 2) out vec2 outUV;

void main() {
    worldPosition = model.data * vec4(position, 1.0);
    outNormal =  mat3(model.data) * normal;
    outUV = inUV;

    gl_Position = camera.proj * camera.view * worldPosition;
}