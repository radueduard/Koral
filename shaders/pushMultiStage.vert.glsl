#version 450 core

// Half of the multi-stage push-constant test: this stage reads only `offset`, the fragment stage
// reads only `color`, and both come from one block declared in a shared header.
#include "pushMultiStage.h.glsl"

void main() {
    // A full-screen triangle, shifted by a push constant so the vertex stage's half of the block
    // is observable in the readback.
    const vec2 positions[3] = vec2[3](vec2(-1.0, -1.0), vec2(3.0, -1.0), vec2(-1.0, 3.0));
    gl_Position = vec4(positions[gl_VertexIndex] + push.offset, 0.0, 1.0);
}
