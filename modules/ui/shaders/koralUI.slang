// koral-ui element shaders, in Slang: the contract a custom element is written against.
// Mirrors koralUICommon.glsl and koralUI.glsl — change them together.
//
//     import koralUI;
//
//     struct Params { float4 from; float4 to; float speed; };         // optional: what each element is given
//     [[vk::binding(0, 1)]] StructuredBuffer<Params> parameters;
//
//     [shader("fragment")]
//     float4 fragmentMain(KuiVaryings v) : SV_Target {
//         KuiFragment f = kuiFragment(v);
//         Params p = parameters[f.parameters];
//         return kuiFinish(f, lerp(p.from, p.to, 0.5 + 0.5 * sin(f.uv.x * 6.0 + f.time * p.speed)));
//     }
//
// The colour given to kuiFinish is straight alpha in sRGB, as colours are written everywhere else in the
// UI; kuiFinish does the rounded corners, the clipping, the anti-aliased edge and the colour encoding.
module koralUI;

public struct KuiInstance {
    public float4 bounds;
    public float4 xform;
    public float2 translate;
    public uint layerClip;
    public uint kindFlags;
    public float4 shape0;
    public float4 shape1;
    public uint fill;
    public uint stroke;
    public float strokeWidth;
    public uint paint;
};

public struct KuiLayer {
    public float4 m;
    public float2 t;
    public float opacity;
    public uint pad;
};

public struct KuiClip {
    public float4 m;
    public float2 t;
    public uint parent;
    public uint pad;
    public float4 rect;
    public float4 radii;
};

public struct KuiPush {
    public float2 viewport;
    public float scale;
    public uint flags;
    public float time;
};

[[vk::binding(0, 0)]] public StructuredBuffer<KuiInstance> kuiInstances;
[[vk::binding(1, 0)]] public StructuredBuffer<KuiLayer> kuiLayers;
[[vk::binding(2, 0)]] public StructuredBuffer<KuiClip> kuiClips;
[[vk::push_constant]] public ConstantBuffer<KuiPush> kuiPush;

/** What the UI's vertex stage hands an element's fragment stage. */
public struct KuiVaryings {
    [[vk::location(0)]] public float2 local : LOCAL;
    [[vk::location(1)]] public nointerpolation uint instance : INSTANCE;
    public float4 position : SV_Position;
};

public struct KuiFragment {
    public float2 position;     // from the rectangle's top-left, in the UI's logical units
    public float2 size;         // the rectangle's size, in logical units
    public float2 uv;           // position / size
    public float pixel;         // logical units per pixel
    public float time;          // seconds
    public uint parameters;     // which entry of the parameters this element was given
    public uint instance;
    public float2 local;        // internal: where in the instance's own space
    public float2 screen;       // internal: logical screen position
};

static const uint KUI_NONE = 0xffff;

float2 kuiApply(float4 m, float2 t, float2 p) { return float2(m.x * p.x + m.z * p.y, m.y * p.x + m.w * p.y) + t; }

public float kuiRoundRect(float2 p, float2 halfSize, float4 radii) {
    float r = p.x > 0.0 ? (p.y > 0.0 ? radii.z : radii.y) : (p.y > 0.0 ? radii.w : radii.x);
    r = min(r, min(halfSize.x, halfSize.y));
    float2 q = abs(p) - halfSize + r;
    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - r;
}

float3 kuiSrgbToLinear(float3 c) {
    return select(c > 0.04045, pow((c + 0.055) / 1.055, 2.4), c / 12.92);
}

float kuiClipCoverage(uint clip, float2 screen, float pixel) {
    float coverage = 1.0;
    for (int depth = 0; depth < 16 && clip != KUI_NONE; ++depth) {
        KuiClip c = kuiClips[clip];
        float2 local = kuiApply(c.m, c.t, screen);
        float2 center = (c.rect.xy + c.rect.zw) * 0.5;
        float d = kuiRoundRect(local - center, (c.rect.zw - c.rect.xy) * 0.5, c.radii);
        float unit = pixel * length(float2(c.m.x, c.m.y));
        coverage *= saturate(0.5 - d / max(unit, 1e-6));
        if (coverage <= 0.0) return 0.0;
        clip = c.parent;
    }
    return coverage;
}

/** The element's fragment: where it is in its rectangle, and what it was given. */
public KuiFragment kuiFragment(KuiVaryings v) {
    KuiInstance it = kuiInstances[v.instance];
    KuiFragment f;
    f.local = v.local;
    f.position = v.local - it.shape0.xy;
    f.size = it.shape0.zw - it.shape0.xy;
    f.uv = f.position / max(f.size, float2(1e-6));
    f.pixel = max(0.5 * (fwidth(v.local.x) + fwidth(v.local.y)), 1e-6);
    f.time = kuiPush.time;
    f.parameters = it.paint;
    f.instance = v.instance;
    f.screen = v.position.xy / kuiPush.scale;
    return f;
}

/** @p color (straight alpha, sRGB) as the target wants it: cut to the element's outline and clips, premultiplied. */
public float4 kuiFinish(KuiFragment f, float4 color) {
    KuiInstance it = kuiInstances[f.instance];
    float2 c = (it.shape0.xy + it.shape0.zw) * 0.5;
    float d = kuiRoundRect(f.local - c, f.size * 0.5, it.shape1);
    float edge = saturate(0.5 - d / f.pixel);
    if ((kuiPush.flags & 1u) != 0u) color.rgb = kuiSrgbToLinear(color.rgb);
    color = float4(color.rgb * color.a, color.a);
    float opacity = float((it.fill >> 24) & 0xffu) / 255.0;
    color *= edge * opacity;
    color *= kuiClipCoverage(it.layerClip & 0xffffu, f.screen, 1.0 / kuiPush.scale);
    color *= kuiLayers[it.layerClip >> 16].opacity;
    return color;
}
