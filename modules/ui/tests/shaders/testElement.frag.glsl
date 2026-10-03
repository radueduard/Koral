#version 450
// A test element: the left half one parameter colour, the right half the other.
#include <koralUI.glsl>

struct Params { vec4 left; vec4 right; };
KUI_PARAMETERS(Params)

vec4 kuiShade(KuiFragment f) {
    const Params p = kuiParameters(f);
    return f.uv.x < 0.5 ? p.left : p.right;
}
