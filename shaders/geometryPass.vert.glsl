#version 450 core

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec3 tangent;
layout(location = 3) in vec3 bitangent;
layout(location = 4) in vec2 inUV;

layout(set = 0, binding = 0) uniform Camera {
    mat4 view;
    mat4 proj;
} camera;

layout(set = 1, binding = 0) uniform Model {
    mat4 model;
};

layout(location = 0) out vec4 worldPosition;
layout(location = 1) out vec3 outNormal;
layout(location = 2) out vec3 outTangent;
layout(location = 3) out vec3 outBitangent;
layout(location = 4) out vec2 outUV;

void main() {
    worldPosition = model * vec4(position, 1.0);
    outNormal =  mat3(model) * normal;
    outTangent = mat3(model) * tangent;
    outBitangent = mat3(model) * bitangent;
    outUV = inUV;

    gl_Position = camera.proj * camera.view * worldPosition;
}