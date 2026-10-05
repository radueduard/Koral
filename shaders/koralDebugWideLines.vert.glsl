#version 450
// Debug lines wider than a pixel (kor::DebugDraw): each a quad facing the screen, six vertices a line,
// its two ends pulled from the storage buffer — the width, in pixels, in the first end's w.

struct Vertex {
    vec4 position;
    vec4 color;
};

layout(std430, set = 0, binding = 0) readonly buffer Lines { Vertex vertices[]; };

layout(push_constant) uniform Push {
    mat4 viewProjection;
    vec2 viewport;      // the target's size, in pixels
    uint firstVertex;   // where the batch's lines start in the buffer
} push;

layout(location = 0) out vec4 outColor;

void main() {
    const uint line = uint(gl_VertexIndex) / 6u;
    const uint corner = uint(gl_VertexIndex) % 6u;
    const Vertex from = vertices[push.firstVertex + line * 2u];
    const Vertex to = vertices[push.firstVertex + line * 2u + 1u];
    const float width = max(from.position.w, 1.0);

    vec4 a = push.viewProjection * vec4(from.position.xyz, 1.0);
    vec4 b = push.viewProjection * vec4(to.position.xyz, 1.0);
    // Cut at the near plane (z = 0 in Vulkan's clip space): an end behind the camera has no place on screen.
    if (a.z < 0.0 && b.z < 0.0) { gl_Position = vec4(0.0, 0.0, 2.0, 1.0); outColor = vec4(0.0); return; }
    if (a.z < 0.0) a = mix(a, b, a.z / (a.z - b.z));
    else if (b.z < 0.0) b = mix(b, a, b.z / (b.z - a.z));

    // Along the line and across it, in pixels; the ends pushed out by half the width so lines meet.
    const vec2 sa = a.xy / a.w * 0.5 * push.viewport, sb = b.xy / b.w * 0.5 * push.viewport;
    const vec2 along = length(sb - sa) > 1e-4 ? normalize(sb - sa) : vec2(1.0, 0.0);
    const vec2 across = vec2(-along.y, along.x);

    // Two triangles: (a-, b-, b+) and (a-, b+, a+).
    const bool atB = corner == 1u || corner == 2u || corner == 4u;
    const float side = (corner == 0u || corner == 1u || corner == 3u) ? -1.0 : 1.0;
    const vec4 end = atB ? b : a;
    const vec2 offset = (across * side + along * (atB ? 1.0 : -1.0)) * (width * 0.5);
    gl_Position = vec4(end.xy + offset / (0.5 * push.viewport) * end.w, end.z, end.w);
    outColor = (atB ? to : from).color;
}
