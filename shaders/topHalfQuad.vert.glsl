#version 450 core

// A descriptor-free quad covering the TOP half of clip space under Koral's
// canonical Y-down NDC: clip y in [-1, 0] (the top), full width x in [-1, 1].
// Six vertices generated purely from gl_VertexIndex, so a Draw(6) paints the top
// half of the target with no vertex/descriptor inputs. Deliberately asymmetric in
// Y so a vertical flip is detectable — the companion of topHalfImage.comp.glsl,
// which writes the same pattern via imageStore.
void main() {
    const vec2 verts[6] = vec2[6](
        vec2(-1.0, -1.0), vec2( 1.0, -1.0), vec2(-1.0, 0.0),
        vec2( 1.0, -1.0), vec2( 1.0,  0.0), vec2(-1.0, 0.0)
    );
    gl_Position = vec4(verts[gl_VertexIndex], 0.0, 1.0);
}
