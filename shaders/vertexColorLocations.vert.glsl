#version 450 core

// The same interleaved position+colour vertex as vertexColor.vert.glsl, but annotated with
// nothing: the layout that feeds it names no semantics either, and says outright which location
// each of its attributes is read at (kor::VertexLayout::Attribute::AtLocation).
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;

layout(location = 0) out vec3 fragColor;

void main() {
    gl_Position = vec4(inPosition, 1.0);
    fragColor = inColor;
}
