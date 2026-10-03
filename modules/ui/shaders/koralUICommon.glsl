// koral-ui: the tables every UI shader reads. Mirrors modules/ui/src/gpu.h — change both together.
//
// One frame of UI is a handful of storage buffers: the instances (one per primitive, glyph, image or
// custom element), the vertices of tessellated paths, the layers (what a retained picture is moved
// by — a scroll is one of these changing), the clips and the gradients. Every draw of the frame reads
// the same set, so the whole UI binds once.

#extension GL_EXT_nonuniform_qualifier : require

#ifndef KUI_COMMON_GLSL
#define KUI_COMMON_GLSL

// ---- instance kinds (Instance.kindFlags & 0xff) -----------------------------------------------------
#define KUI_RECT      0u   // shape0 = rect (x0 y0 x1 y1), shape1 = corner radii (tl tr br bl)
#define KUI_ELLIPSE   1u   // shape0 = center.xy, radii.xy
#define KUI_ARC       2u   // shape0 = center.xy, radius; shape1 = start, sweep (radians), useCenter
#define KUI_SEGMENT   3u   // shape0 = p0.xy p1.xy; shape1.x = cap (0 butt, 1 round, 2 square)
#define KUI_TRIANGLE  4u   // shape0 = p0.xy p1.xy; shape1.xy = p2
#define KUI_BEZIER    5u   // shape0 = p0.xy p1.xy (p1 = control); shape1.xy = p2
#define KUI_GLYPH     6u   // shape0 = rect; shape1 = uv rect; strokeWidth = SDF units -> local units
#define KUI_IMAGE     7u   // shape0 = rect; shape1 = uv rect
#define KUI_SHADOW    8u   // shape0 = rect; shape1.x = corner radius; strokeWidth = sigma
#define KUI_MESH      9u   // tessellated: vertices point back at it for transform, clip and paint
#define KUI_CUSTOM   10u   // shape0 = rect; paint = index into the element shader's parameters

// ---- flags (Instance.kindFlags >> 8 & 0xff) ---------------------------------------------------------
#define KUI_FLAG_FILL      1u
#define KUI_FLAG_STROKE    2u
#define KUI_FLAG_GRADIENT  4u   // the fill comes from gradients[paint]

#define KUI_NONE 0xffffu

struct KuiInstance {
    vec4 bounds;        // local-space quad to rasterize: x0 y0 x1 y1
    vec4 xform;         // local -> layer: x' = a x + c y + tx, y' = b x + d y + ty  (a b c d)
    vec2 translate;
    uint layerClip;     // layer << 16 | clip
    uint kindFlags;     // texture << 16 | flags << 8 | kind
    vec4 shape0;
    vec4 shape1;
    uint fill;          // RGBA8, straight alpha, sRGB-encoded
    uint stroke;
    float strokeWidth;
    uint paint;
};

struct KuiVertex {
    vec2 position;      // local space of its instance
    float coverage;     // 1 inside, 0 at the outer edge of the anti-aliasing fringe
    uint instance;
};

struct KuiLayer {
    vec4 m;             // a b c d
    vec2 t;
    float opacity;
    uint pad;
};

struct KuiClip {
    vec4 m;             // logical screen -> clip local (a b c d)
    vec2 t;
    uint parent;        // KUI_NONE at the end of the chain
    uint pad;
    vec4 rect;          // x0 y0 x1 y1 in clip local
    vec4 radii;         // tl tr br bl
};

struct KuiGradient {
    vec4 geometry;      // linear: start.xy end.xy; radial: center.xy radius 0; sweep: center.xy start 0
    uint type;          // 0 linear, 1 radial, 2 sweep
    uint count;         // stops in use, up to 8
    uint pad0, pad1;
    uint colors[8];
    float stops[8];
};

// Named blocks, named as koralUI.slang names its buffers: a pipeline mixing the two languages
// (the UI's GLSL vertex stage, a Slang element) must agree on each binding's name.
layout(std430, set = 0, binding = 0) readonly buffer KuiInstances { KuiInstance items[]; } kuiInstances;
layout(std430, set = 0, binding = 1) readonly buffer KuiLayers { KuiLayer items[]; } kuiLayers;
layout(std430, set = 0, binding = 2) readonly buffer KuiClips { KuiClip items[]; } kuiClips;
layout(std430, set = 0, binding = 3) readonly buffer KuiGradients { KuiGradient items[]; } kuiGradients;
layout(std430, set = 0, binding = 4) readonly buffer KuiVertices { KuiVertex items[]; } kuiVertices;
layout(set = 0, binding = 5) uniform sampler kuiSampler;
// The draw order: which instance (or mesh vertex) each place in a draw is. What lets every layer keep
// its instances where they are while the frame is still drawn in one go, in paint order.
layout(std430, set = 0, binding = 6) readonly buffer KuiInstanceOrder { uint items[]; } kuiInstanceOrder;
layout(std430, set = 0, binding = 7) readonly buffer KuiVertexOrder { uint items[]; } kuiVertexOrder;
// Declared only by the shaders that sample it (KUI_TEXTURES): a stage that names it without indexing
// it dynamically reflects it as one texture, not an unbounded array, and the stages would disagree.
#ifdef KUI_TEXTURES
layout(set = 0, binding = 8) uniform texture2D kuiTextures[];
#endif

layout(push_constant) uniform KuiPush {
    vec2 viewport;      // in pixels
    float scale;        // pixels per logical unit
    uint flags;         // 1: the target is sRGB, so colours are linearised before blending
    float time;         // seconds, for animated element shaders
} kuiPush;

// ---- helpers ------------------------------------------------------------------------------------------

uint kuiKind(KuiInstance it)    { return it.kindFlags & 0xffu; }
uint kuiFlags(KuiInstance it)   { return (it.kindFlags >> 8) & 0xffu; }
uint kuiTexture(KuiInstance it) { return it.kindFlags >> 16; }
uint kuiLayerOf(KuiInstance it) { return it.layerClip >> 16; }
uint kuiClipOf(KuiInstance it)  { return it.layerClip & 0xffffu; }

vec2 kuiApply(vec4 m, vec2 t, vec2 p) { return vec2(m.x * p.x + m.z * p.y, m.y * p.x + m.w * p.y) + t; }

// Local -> logical screen: the instance's own transform, then its layer's.
vec2 kuiToScreen(KuiInstance it, vec2 local) {
    const KuiLayer layer = kuiLayers.items[kuiLayerOf(it)];
    return kuiApply(layer.m, layer.t, kuiApply(it.xform, it.translate, local));
}

// The 2x2 part of local -> pixels, for how big a local unit is on screen.
mat2 kuiJacobian(KuiInstance it) {
    const KuiLayer layer = kuiLayers.items[kuiLayerOf(it)];
    const mat2 l = mat2(layer.m.x, layer.m.y, layer.m.z, layer.m.w);
    const mat2 i = mat2(it.xform.x, it.xform.y, it.xform.z, it.xform.w);
    return kuiPush.scale * l * i;
}

vec4 kuiUnpack(uint c) { return unpackUnorm4x8(c); }

vec3 kuiSrgbToLinear(vec3 c) {
    return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), greaterThan(c, vec3(0.04045)));
}

// A straight-alpha sRGB colour, as the target wants it, premultiplied.
vec4 kuiPremultiplied(vec4 c) {
    if ((kuiPush.flags & 1u) != 0u) c.rgb = kuiSrgbToLinear(c.rgb);
    return vec4(c.rgb * c.a, c.a);
}

vec4 kuiColor(uint packedColor) { return kuiPremultiplied(kuiUnpack(packedColor)); }

// ---- signed distances (negative inside) ----------------------------------------------------------------

float kuiRoundRect(vec2 p, vec2 halfSize, vec4 radii) {
    // radii: tl tr br bl, with y growing downwards.
    float r = p.x > 0.0 ? (p.y > 0.0 ? radii.z : radii.y) : (p.y > 0.0 ? radii.w : radii.x);
    r = min(r, min(halfSize.x, halfSize.y));
    const vec2 q = abs(p) - halfSize + r;
    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - r;
}

float kuiEllipse(vec2 p, vec2 ab) {
    // A cheap, smooth approximation that is exact for circles and good to well under a pixel elsewhere.
    if (abs(ab.x - ab.y) < 1e-4) return length(p) - ab.x;
    const float k0 = length(p / ab);
    const float k1 = length(p / (ab * ab));
    return k0 * (k0 - 1.0) / max(k1, 1e-6);
}

float kuiSegment(vec2 p, vec2 a, vec2 b) {
    const vec2 pa = p - a, ba = b - a;
    const float h = clamp(dot(pa, ba) / max(dot(ba, ba), 1e-9), 0.0, 1.0);
    return length(pa - ba * h);
}

float kuiTriangle(vec2 p, vec2 p0, vec2 p1, vec2 p2) {
    const vec2 e0 = p1 - p0, e1 = p2 - p1, e2 = p0 - p2;
    const vec2 v0 = p - p0, v1 = p - p1, v2 = p - p2;
    const vec2 pq0 = v0 - e0 * clamp(dot(v0, e0) / dot(e0, e0), 0.0, 1.0);
    const vec2 pq1 = v1 - e1 * clamp(dot(v1, e1) / dot(e1, e1), 0.0, 1.0);
    const vec2 pq2 = v2 - e2 * clamp(dot(v2, e2) / dot(e2, e2), 0.0, 1.0);
    const float s = sign(e0.x * e2.y - e0.y * e2.x);
    const vec2 d = min(min(vec2(dot(pq0, pq0), s * (v0.x * e0.y - v0.y * e0.x)),
                           vec2(dot(pq1, pq1), s * (v1.x * e1.y - v1.y * e1.x))),
                           vec2(dot(pq2, pq2), s * (v2.x * e2.y - v2.y * e2.x)));
    return -sqrt(d.x) * sign(d.y);
}

// Unsigned distance to a quadratic Bezier (Inigo Quilez).
float kuiBezier(vec2 pos, vec2 A, vec2 B, vec2 C) {
    vec2 a = B - A;
    vec2 b = A - 2.0 * B + C;
    if (dot(b, b) < 1e-6) return kuiSegment(pos, A, C);
    vec2 c = a * 2.0;
    vec2 d = A - pos;
    float kk = 1.0 / dot(b, b);
    float kx = kk * dot(a, b);
    float ky = kk * (2.0 * dot(a, a) + dot(d, b)) / 3.0;
    float kz = kk * dot(d, a);
    float res;
    float p = ky - kx * kx;
    float p3 = p * p * p;
    float q = kx * (2.0 * kx * kx - 3.0 * ky) + kz;
    float h = q * q + 4.0 * p3;
    if (h >= 0.0) {
        h = sqrt(h);
        vec2 x = (vec2(h, -h) - q) / 2.0;
        vec2 uv = sign(x) * pow(abs(x), vec2(1.0 / 3.0));
        float t = clamp(uv.x + uv.y - kx, 0.0, 1.0);
        vec2 qv = d + (c + b * t) * t;
        res = dot(qv, qv);
    } else {
        float z = sqrt(-p);
        float v = acos(q / (p * z * 2.0)) / 3.0;
        float m = cos(v);
        float n = sin(v) * 1.732050808;
        vec3 t = clamp(vec3(m + m, -n - m, n - m) * z - kx, 0.0, 1.0);
        vec2 qx = d + (c + b * t.x) * t.x;
        float dx = dot(qx, qx);
        vec2 qy = d + (c + b * t.y) * t.y;
        float dy = dot(qy, qy);
        res = min(dx, dy);
    }
    return sqrt(res);
}

#define KUI_PI 3.14159265358979

// Angle of p in [0, 2pi), measured as atan2 with y down (clockwise on screen), from +x.
float kuiAngle(vec2 p) {
    float a = atan(p.y, p.x);
    return a < 0.0 ? a + 2.0 * KUI_PI : a;
}

// Distance to the arc of radius r from `start` sweeping `sweep` radians: the circle where the angle
// is inside the sweep, else the nearer end point.
float kuiArc(vec2 p, float r, float start, float sweep) {
    if (sweep < 0.0) { start += sweep; sweep = -sweep; }
    if (sweep >= 2.0 * KUI_PI) return abs(length(p) - r);
    float rel = mod(kuiAngle(p) - start, 2.0 * KUI_PI);
    if (rel <= sweep) return abs(length(p) - r);
    const vec2 a = r * vec2(cos(start), sin(start));
    const vec2 b = r * vec2(cos(start + sweep), sin(start + sweep));
    return min(length(p - a), length(p - b));
}

// Signed distance to a pie slice (the arc plus its centre): the distance to its boundary — arc and
// two radii — negative inside. Exact for any sweep, convex or not.
float kuiPie(vec2 p, float r, float start, float sweep) {
    if (sweep < 0.0) { start += sweep; sweep = -sweep; }
    if (sweep >= 2.0 * KUI_PI) return length(p) - r;
    const vec2 a = r * vec2(cos(start), sin(start));
    const vec2 b = r * vec2(cos(start + sweep), sin(start + sweep));
    const float boundary = min(kuiArc(p, r, start, sweep), min(kuiSegment(p, vec2(0.0), a), kuiSegment(p, vec2(0.0), b)));
    const bool inside = length(p) < r && mod(kuiAngle(p) - start, 2.0 * KUI_PI) <= sweep;
    return inside ? -boundary : boundary;
}

// ---- clips ----------------------------------------------------------------------------------------------

// How much of the pixel at logical screen position @p screen the clip chain from @p clip lets through.
float kuiClipCoverage(uint clip, vec2 screen, float pixel) {
    float coverage = 1.0;
    for (int depth = 0; depth < 16 && clip != KUI_NONE; ++depth) {
        const KuiClip c = kuiClips.items[clip];
        const vec2 local = kuiApply(c.m, c.t, screen);
        const vec2 center = (c.rect.xy + c.rect.zw) * 0.5;
        const float d = kuiRoundRect(local - center, (c.rect.zw - c.rect.xy) * 0.5, c.radii);
        // The clip's local units, in pixels: its scale is what the inverse took out.
        const float unit = pixel * length(vec2(c.m.x, c.m.y));
        coverage *= clamp(0.5 - d / max(unit, 1e-6), 0.0, 1.0);
        if (coverage <= 0.0) return 0.0;
        clip = c.parent;
    }
    return coverage;
}

// ---- gradients --------------------------------------------------------------------------------------------

vec4 kuiGradient(uint index, vec2 p) {
    const KuiGradient g = kuiGradients.items[index];
    float t;
    if (g.type == 0u) {
        const vec2 d = g.geometry.zw - g.geometry.xy;
        t = dot(p - g.geometry.xy, d) / max(dot(d, d), 1e-9);
    } else if (g.type == 1u) {
        t = length(p - g.geometry.xy) / max(g.geometry.z, 1e-9);
    } else {
        t = mod(kuiAngle(p - g.geometry.xy) - g.geometry.z, 2.0 * KUI_PI) / (2.0 * KUI_PI);
    }
    t = clamp(t, 0.0, 1.0);
    // Interpolated in premultiplied space, as the target sees it.
    vec4 result = kuiColor(g.colors[0]);
    for (uint i = 1u; i < g.count; ++i) {
        const float a = g.stops[i - 1u], b = g.stops[i];
        if (t <= a) break;
        result = mix(kuiColor(g.colors[i - 1u]), kuiColor(g.colors[i]), clamp((t - a) / max(b - a, 1e-9), 0.0, 1.0));
    }
    return result;
}

vec4 kuiFillColor(KuiInstance it, vec2 local) {
    if ((kuiFlags(it) & KUI_FLAG_GRADIENT) != 0u) return kuiGradient(it.paint, local) * kuiUnpack(it.fill).a;
    return kuiColor(it.fill);
}

#endif
