#version 450
#include <koralUICommon.glsl>

layout(location = 0) in vec2 vLocal;
layout(location = 1) flat in uint vInstance;
layout(location = 2) in float vCoverage;
layout(location = 0) out vec4 outColor;

void main() {
    const KuiInstance it = kuiInstances.items[vInstance];
    // A stroke's triangles carry its colour in `stroke`, a fill's in `fill` (or its gradient).
    vec4 color = (kuiFlags(it) & KUI_FLAG_STROKE) != 0u ? kuiColor(it.stroke) : kuiFillColor(it, vLocal);
    color *= clamp(vCoverage, 0.0, 1.0);
    color *= kuiClipCoverage(kuiClipOf(it), gl_FragCoord.xy / kuiPush.scale, 1.0 / kuiPush.scale);
    color *= kuiLayers.items[kuiLayerOf(it)].opacity;
    outColor = color;
}
