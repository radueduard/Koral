#version 450
// An image among the debug shapes (kor::DebugDraw): a quad, six vertices, its four corners pulled from the storage
// buffer — bottom left, bottom right, top right and top left of the picture — its tint with them.

struct Vertex {
    vec4 position;
    vec4 color;
};

layout(std430, set = 0, binding = 0) readonly buffer Lines { Vertex vertices[]; };

layout(push_constant) uniform Push {
    mat4 viewProjection;
    uint firstVertex;   // where the image's corners start in the buffer
} push;

layout(location = 0) out vec2 outUv;
layout(location = 1) out vec4 outTint;

const uint corners[6] = uint[](0u, 1u, 2u, 0u, 2u, 3u);
const vec2 uvs[4] = vec2[](vec2(0.0, 1.0), vec2(1.0, 1.0), vec2(1.0, 0.0), vec2(0.0, 0.0));   // a picture's first row is its top

void main() {
    const uint corner = corners[gl_VertexIndex];
    const Vertex v = vertices[push.firstVertex + corner];
    gl_Position = push.viewProjection * vec4(v.position.xyz, 1.0);
    outUv = uvs[corner];
    outTint = v.color;
}
