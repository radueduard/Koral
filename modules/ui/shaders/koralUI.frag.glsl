#version 450
// koral-ui's built-in element: every primitive the canvas draws — rectangles, ellipses, arcs, lines,
// triangles, curves, glyphs, images and shadows — as a signed distance, so each is one quad with an
// exact anti-aliased edge at any scale, and a whole interface is one instanced draw.

#define KUI_TEXTURES
#include <koralUICommon.glsl>

layout(location = 0) in vec2 vLocal;
layout(location = 1) flat in uint vInstance;
layout(location = 0) out vec4 outColor;

// Coverage of a pixel by the region where distance < 0, @p w being a pixel in local units.
float cover(float d, float w) { return clamp(0.5 - d / w, 0.0, 1.0); }

// Gaussian-blurred rounded box (Evan Wallace's closed form along x, integrated along y).
vec2 erfApprox(vec2 x) {
    const vec2 s = sign(x), a = abs(x);
    x = 1.0 + (0.278393 + (0.230389 + 0.078108 * (a * a)) * a) * a;
    x *= x;
    return s - s / (x * x);
}
float gaussian(float x, float sigma) { return exp(-(x * x) / (2.0 * sigma * sigma)) / (2.5066282746 * sigma); }
float shadowX(float x, float y, float sigma, float corner, vec2 halfSize) {
    const float delta = min(halfSize.y - corner - abs(y), 0.0);
    const float curved = halfSize.x - corner + sqrt(max(0.0, corner * corner - delta * delta));
    const vec2 integral = 0.5 + 0.5 * erfApprox((x + vec2(-curved, curved)) * (sqrt(0.5) / sigma));
    return integral.y - integral.x;
}
float shadow(vec2 lower, vec2 upper, vec2 point, float sigma, float corner) {
    const vec2 center = (lower + upper) * 0.5, halfSize = (upper - lower) * 0.5;
    point -= center;
    const float low = point.y - halfSize.y, high = point.y + halfSize.y;
    const float start = clamp(-3.0 * sigma, low, high), end = clamp(3.0 * sigma, low, high);
    const float step = (end - start) / 4.0;
    float y = start + step * 0.5, value = 0.0;
    for (int i = 0; i < 4; ++i) {
        value += shadowX(point.x, point.y - y, sigma, corner, halfSize) * gaussian(y, sigma) * step;
        y += step;
    }
    return value;
}

void main() {
    const KuiInstance it = kuiInstances.items[vInstance];
    const uint kind = kuiKind(it);
    const uint flags = kuiFlags(it);
    const vec2 p = vLocal;

    // A pixel, in local units: what turns a distance into coverage.
    const float w = max(0.5 * (fwidth(p.x) + fwidth(p.y)), 1e-6);

    vec4 color = vec4(0.0);
    if (kind == KUI_GLYPH || kind == KUI_IMAGE) {
        const vec2 t = (p - it.shape0.xy) / max(it.shape0.zw - it.shape0.xy, vec2(1e-6));
        const vec2 uv = mix(it.shape1.xy, it.shape1.zw, t);
        const vec4 texel = texture(sampler2D(kuiTextures[nonuniformEXT(kuiTexture(it))], kuiSampler), uv);
        if (kind == KUI_GLYPH) {
            // The atlas holds distance from the outline, 0.5 on it; strokeWidth scales it to local units.
            // stroke carries how far the outline is moved out — text heavier than its font — or in.
            const float d = (0.50196 - texel.r) * it.strokeWidth - uintBitsToFloat(it.stroke);
            color = kuiFillColor(it, p) * cover(d, w);
        } else {
            // Clipped to the rectangle with an anti-aliased edge, like any other shape.
            const vec2 c = (it.shape0.xy + it.shape0.zw) * 0.5;
            const float d = kuiRoundRect(p - c, (it.shape0.zw - it.shape0.xy) * 0.5, vec4(0.0));
            const vec4 tint = kuiUnpack(it.fill);
            // Sampled as the image's format says (an sRGB image arrives linear), then premultiplied.
            color = vec4(texel.rgb * texel.a, texel.a) * kuiPremultiplied(tint) * cover(d, w);
        }
    } else if (kind == KUI_BACKDROP) {
        // Glass: what is behind — a picture of the target, half its size, with its smaller copies —
        // blurred by reading a smaller copy at a ring of places, bent towards the middle near the edge
        // as a lens bends it, and tinted.
        const vec2 c = (it.shape0.xy + it.shape0.zw) * 0.5;
        const vec2 halfSize = (it.shape0.zw - it.shape0.xy) * 0.5;
        const float d = kuiRoundRect(p - c, halfSize, it.shape1);
        const float sigma = max(it.strokeWidth * kuiPush.scale, 0.0);     // in pixels
        const float bend = uintBitsToFloat(it.stroke) * kuiPush.scale;
        vec2 at = gl_FragCoord.xy;
        if (bend > 0.0) {
            // Which way out is, on screen: the way the distance grows.
            const vec2 g = vec2(dFdx(d), dFdy(d));
            const float rim = clamp(1.0 + d / max(min(halfSize.x, halfSize.y) * 0.6, 1e-3), 0.0, 1.0);
            at -= normalize(g + vec2(1e-6)) * bend * rim * rim * rim;
        }
        const vec2 uv = at / kuiPush.viewport;
        const vec2 texel = 1.0 / kuiPush.viewport;
        // The copy whose texels are about as big as the blur's step; level 0 is half the target already.
        const float lod = max(log2(max(sigma, 1.0)) - 1.5, 0.0);
        const float ring = sigma * 1.2;
        vec3 sum = textureLod(sampler2D(kuiTextures[nonuniformEXT(kuiTexture(it))], kuiSampler), uv, lod).rgb * 2.0;
        float weight = 2.0;
        for (int i = 0; i < 8; ++i) {
            const float a = 0.7853982 * float(i) + 0.3926991;
            const vec2 o = vec2(cos(a), sin(a)) * ring * ((i & 1) == 0 ? 1.0 : 0.55);
            sum += textureLod(sampler2D(kuiTextures[nonuniformEXT(kuiTexture(it))], kuiSampler), uv + o * texel, lod).rgb;
            weight += 1.0;
        }
        vec3 behind = sum / weight;
        const vec4 tint = kuiColor(it.fill);        // premultiplied, in the target's space
        behind = behind * (1.0 - tint.a) + tint.rgb;
        color = vec4(behind, 1.0) * cover(d, w);
    } else if (kind == KUI_SHADOW) {
        const float sigma = max(it.strokeWidth, 1e-3);
        color = kuiColor(it.fill) * shadow(it.shape0.xy, it.shape0.zw, p, sigma, it.shape1.x);
    } else {
        // A shape: a signed distance for the fill and, around its outline, the stroke.
        float d;
        bool filled = true;
        if (kind == KUI_RECT) {
            const vec2 c = (it.shape0.xy + it.shape0.zw) * 0.5;
            d = kuiRoundRect(p - c, (it.shape0.zw - it.shape0.xy) * 0.5, it.shape1);
        } else if (kind == KUI_ELLIPSE) {
            d = kuiEllipse(p - it.shape0.xy, it.shape0.zw);
        } else if (kind == KUI_ARC) {
            const vec2 q = p - it.shape0.xy;
            if (it.shape1.z > 0.5) d = kuiPie(q, it.shape0.z, it.shape1.x, it.shape1.y);
            else { d = kuiArc(q, it.shape0.z, it.shape1.x, it.shape1.y); filled = false; }
        } else if (kind == KUI_SEGMENT) {
            // A line is all stroke; its caps decide how far past the ends it reaches.
            const vec2 a = it.shape0.xy, b = it.shape0.zw;
            const float cap = it.shape1.x;
            const float halfWidth = it.strokeWidth * 0.5;
            if (cap < 0.5) {
                const vec2 dir = normalize(b - a + vec2(1e-9, 0.0));
                const vec2 local = vec2(dot(p - a, dir), dot(p - a, vec2(-dir.y, dir.x)));
                const float len = length(b - a);
                const vec2 q = abs(local - vec2(len * 0.5, 0.0)) - vec2(len * 0.5, halfWidth);
                d = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0);
            } else if (cap < 1.5) {
                d = kuiSegment(p, a, b) - halfWidth;
            } else {
                const vec2 dir = normalize(b - a + vec2(1e-9, 0.0));
                const vec2 local = vec2(dot(p - a, dir), dot(p - a, vec2(-dir.y, dir.x)));
                const float len = length(b - a);
                const vec2 q = abs(local - vec2(len * 0.5, 0.0)) - vec2(len * 0.5 + halfWidth, halfWidth);
                d = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0);
            }
            outColor = kuiColor(it.stroke) * cover(d, w) * kuiClipCoverage(kuiClipOf(it), gl_FragCoord.xy / kuiPush.scale, 1.0 / kuiPush.scale);
            outColor *= kuiLayers.items[kuiLayerOf(it)].opacity;
            return;
        } else if (kind == KUI_TRIANGLE) {
            d = kuiTriangle(p, it.shape0.xy, it.shape0.zw, it.shape1.xy);
        } else {   // KUI_BEZIER: a curve has no inside
            d = kuiBezier(p, it.shape0.xy, it.shape0.zw, it.shape1.xy);
            filled = false;
        }

        if (filled && (flags & KUI_FLAG_FILL) != 0u) color = kuiFillColor(it, p) * cover(d, w);
        if ((flags & KUI_FLAG_STROKE) != 0u) {
            const float s = cover(abs(d) - it.strokeWidth * 0.5, w);
            const vec4 stroke = kuiColor(it.stroke) * s;
            color = stroke + color * (1.0 - stroke.a);
        }
    }

    color *= kuiClipCoverage(kuiClipOf(it), gl_FragCoord.xy / kuiPush.scale, 1.0 / kuiPush.scale);
    color *= kuiLayers.items[kuiLayerOf(it)].opacity;
    outColor = color;
}
