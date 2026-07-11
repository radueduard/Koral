#version 450 core

// A descriptor-free full-screen triangle: three vertices generated purely from
// gl_VertexIndex, covering the whole [-1,1] clip region so a Draw(3) paints every
// pixel of the target. Used by the offscreen-render integration test, which needs
// a graphics pipeline that depends on no uniform/vertex buffers.
void main() {
    vec2 uv = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(uv * 2.0 - 1.0, 0.0, 1.0);
}
