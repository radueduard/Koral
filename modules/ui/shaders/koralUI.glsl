// koral-ui element shaders, in GLSL: the contract a custom element is written against.
//
// An element is a rectangle the UI places, and a fragment shader that decides what is in it. Write one
// function, kuiShade, and include this file; the vertex stage, the rounded corners, the clipping, the
// anti-aliased edge and the colour encoding are the UI's.
//
//     #version 450
//     #include <koralUI.glsl>
//
//     struct Params { vec4 from; vec4 to; float speed; };   // optional: what each element is given
//     KUI_PARAMETERS(Params)
//
//     vec4 kuiShade(KuiFragment f) {
//         const Params p = kuiParameters(f);
//         return mix(p.from, p.to, 0.5 + 0.5 * sin(f.uv.x * 6.0 + f.time * p.speed));
//     }
//
// kuiShade returns a straight-alpha colour in sRGB, as colours are written everywhere else in the UI.
// Elements drawn with the same shader are drawn together, in one instanced draw.

#ifndef KUI_ELEMENT_GLSL
#define KUI_ELEMENT_GLSL

#include <koralUICommon.glsl>

struct KuiFragment {
    vec2 position;      // from the rectangle's top-left, in the UI's logical units
    vec2 size;          // the rectangle's size, in logical units
    vec2 uv;            // position / size: 0 at the top-left, 1 at the bottom-right
    float pixel;        // how many logical units one pixel is — for anti-aliasing your own edges
    float time;         // seconds
    uint parameters;    // which entry of the parameters this element was given
    uint instance;
};

// Declares the per-element parameter block, as set 1, binding 0.
#define KUI_PARAMETERS(T) \
    layout(std430, set = 1, binding = 0) readonly buffer KuiParameterBlock { T kuiParameterArray[]; }; \
    T kuiParameters(KuiFragment f) { return kuiParameterArray[f.parameters]; }

vec4 kuiShade(KuiFragment f);

layout(location = 0) in vec2 vLocal;
layout(location = 1) flat in uint vInstance;
layout(location = 0) out vec4 kuiOutColor;

void main() {
    const KuiInstance it = kuiInstances.items[vInstance];
    KuiFragment f;
    f.position = vLocal - it.shape0.xy;
    f.size = it.shape0.zw - it.shape0.xy;
    f.uv = f.position / max(f.size, vec2(1e-6));
    f.pixel = max(0.5 * (fwidth(vLocal.x) + fwidth(vLocal.y)), 1e-6);
    f.time = kuiPush.time;
    f.parameters = it.paint;
    f.instance = vInstance;

    // The element's own outline: its rectangle, with the corners it was given.
    const vec2 c = (it.shape0.xy + it.shape0.zw) * 0.5;
    const float d = kuiRoundRect(vLocal - c, f.size * 0.5, it.shape1);
    const float edge = clamp(0.5 - d / f.pixel, 0.0, 1.0);
    if (edge <= 0.0) discard;

    vec4 color = kuiPremultiplied(kuiShade(f));
    color *= edge * kuiUnpack(it.fill).a;
    color *= kuiClipCoverage(kuiClipOf(it), gl_FragCoord.xy / kuiPush.scale, 1.0 / kuiPush.scale);
    color *= kuiLayers.items[kuiLayerOf(it)].opacity;
    kuiOutColor = color;
}

#endif
