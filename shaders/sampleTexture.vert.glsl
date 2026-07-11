#version 450 core

// Descriptor-free full-screen triangle that also forwards a UV, so a fragment
// shader can sample a texture across the whole target. Same vertex generation as
// flatTriangle.vert; UV spans [0,1] over the visible quad.
layout(location = 0) out vec2 outUV;

void main() {
    vec2 uv = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    outUV = uv;
    gl_Position = vec4(uv * 2.0 - 1.0, 0.0, 1.0);
}
