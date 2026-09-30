#version 450
// Debug lines (kor::DebugDraw): pulled from a storage buffer, two vertices a line.

struct Vertex {
    vec4 position;
    vec4 color;
};

layout(std430, set = 0, binding = 0) readonly buffer Lines { Vertex vertices[]; };

layout(push_constant) uniform Push {
    mat4 viewProjection;
} push;

layout(location = 0) out vec4 outColor;

void main() {
    const Vertex v = vertices[gl_VertexIndex];
    gl_Position = push.viewProjection * vec4(v.position.xyz, 1.0);
    outColor = v.color;
}
