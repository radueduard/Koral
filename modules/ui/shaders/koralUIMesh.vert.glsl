#version 450
// koral-ui: tessellated paths — fills and strokes the CPU cut into triangles, with a one-pixel fringe
// whose coverage falls to zero for the anti-aliased edge. Each vertex names the instance that holds its
// transform, clip and paint.

#include <koralUICommon.glsl>

layout(location = 0) out vec2 vLocal;
layout(location = 1) flat out uint vInstance;
layout(location = 2) out float vCoverage;

void main() {
    const KuiVertex v = kuiVertices.items[kuiVertexOrder.items[gl_VertexIndex]];
    const KuiInstance it = kuiInstances.items[v.instance];
    vLocal = v.position;
    vInstance = v.instance;
    vCoverage = v.coverage;
    const vec2 pixels = kuiToScreen(it, v.position) * kuiPush.scale;
    gl_Position = vec4(pixels / kuiPush.viewport * 2.0 - 1.0, 0.0, 1.0);
}
