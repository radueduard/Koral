#version 450 core

// Deliberately disagrees with pushConflict.frag.glsl about what `tint` is, to prove the pipeline
// catches two stages placing one push constant differently. There is a single push-constant range
// per pipeline, so both stages read the same bytes — a disagreement here is a shader bug that no
// driver message would explain.
layout(push_constant) uniform Push {
    vec4 tint;
} push;

void main() {
    gl_Position = vec4(push.tint.xy, 0.0, 1.0);
}
