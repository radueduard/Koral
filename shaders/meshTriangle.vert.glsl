#version 450 core

// Reads vertex positions from a bound vertex buffer (kor::Position stream), so the
// mesh-based graphics test drives the pipeline's vertex-input state and the
// BindMesh / DrawIndexed path.
layout(location = 0) in vec3 inPosition;

void main() {
    gl_Position = vec4(inPosition, 1.0);
}
