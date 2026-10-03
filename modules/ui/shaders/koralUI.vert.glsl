#version 450
// koral-ui: one quad per instance, pulled from the instance table — no vertex buffer. The quad is the
// instance's local bounds, grown by a pixel and a half so the anti-aliased edge has room, and carried
// to the screen by the instance's transform and then its layer's.

#include <koralUICommon.glsl>

layout(location = 0) out vec2 vLocal;
layout(location = 1) flat out uint vInstance;

void main() {
    const vec2 corners[6] = vec2[6](vec2(0, 0), vec2(1, 0), vec2(0, 1), vec2(0, 1), vec2(1, 0), vec2(1, 1));
    const uint id = kuiInstanceOrder.items[gl_InstanceIndex];
    const KuiInstance it = kuiInstances.items[id];

    // How many local units a pixel is, so the fringe is a pixel and a half wherever it is drawn.
    const mat2 j = kuiJacobian(it);
    const float pad = 1.5 / sqrt(max(abs(determinant(j)), 1e-12));
    const vec2 lo = it.bounds.xy - pad, hi = it.bounds.zw + pad;

    const vec2 local = mix(lo, hi, corners[gl_VertexIndex % 6]);
    const vec2 pixels = kuiToScreen(it, local) * kuiPush.scale;

    vLocal = local;
    vInstance = id;
    gl_Position = vec4(pixels / kuiPush.viewport * 2.0 - 1.0, 0.0, 1.0);
}
