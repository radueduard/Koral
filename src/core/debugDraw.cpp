//
// Debug shapes: kept on the CPU, uploaded once a frame, drawn as lines and triangles pulled from a
// buffer. Gizmos are the same shapes, with picking and dragging on top.
//

#include "debugDraw.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <limits>
#include <numbers>

#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "commandBuffer.h"
#include "image.h"
#include "imageView.h"
#include "log.h"
#include "scene.h"
#include "window.h"
#include "shader.h"

namespace kor
{
    namespace {
        constexpr float pi = std::numbers::pi_v<float>;

        /** Two unit vectors at right angles to @p n and to each other. */
        std::pair<glm::vec3, glm::vec3> basisOf(const glm::vec3 n)
        {
            const glm::vec3 helper = std::abs(n.y) < 0.99f ? glm::vec3(0.f, 1.f, 0.f) : glm::vec3(1.f, 0.f, 0.f);
            const glm::vec3 u = glm::normalize(glm::cross(n, helper));
            return {u, glm::cross(n, u)};
        }

        glm::vec3 directionOr(const glm::vec3 v, const glm::vec3 fallback)
        {
            const float length = glm::length(v);
            return length > 1e-8f ? v / length : fallback;
        }

        /** The same style, without its fill: for the parts of a shape that are only lines. */
        DebugStyle linesOf(DebugStyle style)
        {
            style.fill = glm::vec4(0.f);
            return style;
        }
    }

    DebugDraw::DebugDraw() = default;
    DebugDraw::~DebugDraw() = default;

    // ---- building blocks --------------------------------------------------------------------------

    void DebugDraw::Edge(const glm::vec3 from, const glm::vec3 to, const Style& style)
    {
        if (style.outline) Line(from, to, style);
    }

    void DebugDraw::Ring(const glm::vec3 center, const glm::vec3 normal, const float radius, const Style& style, const int segments)
    {
        if (!style.outline) return;
        const auto [u, v] = basisOf(directionOr(normal, {0.f, 1.f, 0.f}));
        const int count = std::max(segments, 3);
        glm::vec3 previous = center + u * radius;
        for (int i = 1; i <= count; ++i) {
            const float angle = 2.f * pi * static_cast<float>(i) / static_cast<float>(count);
            const glm::vec3 next = center + (u * std::cos(angle) + v * std::sin(angle)) * radius;
            Edge(previous, next, style);
            previous = next;
        }
    }

    bool DebugDraw::BeginFill(const Style& style)
    {
        if (style.fill.a <= 0.f) return false;
        _filling = Solid{static_cast<std::uint32_t>(_solidVertices.size()), 0, style.fill, {}, style.duration, style.onTop};
        return true;
    }

    void DebugDraw::FillTriangle(const glm::vec3 a, const glm::vec3 b, const glm::vec3 c)
    {
        _solidVertices.push_back(a);
        _solidVertices.push_back(b);
        _solidVertices.push_back(c);
    }

    void DebugDraw::EndFill()
    {
        if (!_filling) return;
        Solid solid = *_filling;
        _filling.reset();
        solid.count = static_cast<std::uint32_t>(_solidVertices.size()) - solid.first;
        if (solid.count == 0) return;
        // The middle of its bounds: what it is sorted by.
        glm::vec3 min(std::numeric_limits<float>::max()), max(std::numeric_limits<float>::lowest());
        for (std::uint32_t i = solid.first; i < solid.first + solid.count; ++i) {
            min = glm::min(min, _solidVertices[i]);
            max = glm::max(max, _solidVertices[i]);
        }
        solid.center = (min + max) * 0.5f;
        _solids.push_back(solid);
    }

    // ---- lines ------------------------------------------------------------------------------------

    void DebugDraw::Line(const glm::vec3 from, const glm::vec3 to, const Style& style)
    {
        _lines.push_back({from, to, style.color, style.duration, style.onTop, std::max(style.lineWidth, 1.f)});
    }

    void DebugDraw::Arrow(const glm::vec3 from, const glm::vec3 to, const Style& style)
    {
        Line(from, to, style);
        const glm::vec3 along = to - from;
        const float length = glm::length(along);
        if (length <= 0.f) return;
        const glm::vec3 direction = along / length;
        const auto [side, up] = basisOf(direction);
        const float head = length * 0.2f;
        const glm::vec3 base = to - direction * head;
        for (const glm::vec3 offset : {side, -side, up, -up}) Edge(to, base + offset * head * 0.4f, style);
        if (style.fill.a > 0.f) {
            Style cone = style;
            cone.outline = false;
            Cone(base, to, head * 0.4f, cone, 12);
        }
    }

    void DebugDraw::Point(const glm::vec3 position, const float size, const Style& style)
    {
        const float h = size * 0.5f;
        Line(position - glm::vec3(h, 0.f, 0.f), position + glm::vec3(h, 0.f, 0.f), style);
        Line(position - glm::vec3(0.f, h, 0.f), position + glm::vec3(0.f, h, 0.f), style);
        Line(position - glm::vec3(0.f, 0.f, h), position + glm::vec3(0.f, 0.f, h), style);
    }

    void DebugDraw::Axes(const glm::mat4& transform, const float size, const float duration)
    {
        const glm::vec3 origin(transform[3]);
        Arrow(origin, origin + glm::vec3(transform[0]) * size, {.color = {1.f, 0.2f, 0.2f, 1.f}, .duration = duration});
        Arrow(origin, origin + glm::vec3(transform[1]) * size, {.color = {0.2f, 1.f, 0.2f, 1.f}, .duration = duration});
        Arrow(origin, origin + glm::vec3(transform[2]) * size, {.color = {0.3f, 0.5f, 1.f, 1.f}, .duration = duration});
    }

    void DebugDraw::Grid(const glm::vec3 center, const float size, const int cells, const Style& style)
    {
        const int count = std::max(cells, 1);
        const float half = size * 0.5f;
        for (int i = 0; i <= count; ++i) {
            const float t = -half + size * static_cast<float>(i) / static_cast<float>(count);
            Line(center + glm::vec3(t, 0.f, -half), center + glm::vec3(t, 0.f, half), style);
            Line(center + glm::vec3(-half, 0.f, t), center + glm::vec3(half, 0.f, t), style);
        }
    }

    void DebugDraw::Frustum(const glm::mat4& viewProjection, const Style& style)
    {
        // Clip space's corners, back through the inverse: depth 0 to 1, as Vulkan's clip space has it.
        const glm::mat4 inverse = glm::inverse(viewProjection);
        std::array<glm::vec3, 8> corners;
        for (int i = 0; i < 8; ++i) {
            const glm::vec4 clip { (i & 1) ? 1.f : -1.f, (i & 2) ? 1.f : -1.f, (i & 4) ? 1.f : 0.f, 1.f };
            const glm::vec4 world = inverse * clip;
            corners[i] = glm::vec3(world) / world.w;
        }
        for (int i = 0; i < 8; ++i)
            for (const int bit : {1, 2, 4})
                if (!(i & bit)) Line(corners[i], corners[i | bit], style);
    }

    // ---- shapes -----------------------------------------------------------------------------------

    void DebugDraw::Triangle(const glm::vec3 a, const glm::vec3 b, const glm::vec3 c, const Style& style)
    {
        if (BeginFill(style)) {
            FillTriangle(a, b, c);
            EndFill();
        }
        Edge(a, b, style);
        Edge(b, c, style);
        Edge(c, a, style);
    }

    void DebugDraw::Quad(const glm::vec3 a, const glm::vec3 b, const glm::vec3 c, const glm::vec3 d, const Style& style)
    {
        if (BeginFill(style)) {
            FillTriangle(a, b, c);
            FillTriangle(a, c, d);
            EndFill();
        }
        Edge(a, b, style);
        Edge(b, c, style);
        Edge(c, d, style);
        Edge(d, a, style);
    }

    void DebugDraw::Plane(const glm::vec3 center, const glm::vec3 normal, const glm::vec2 size, const Style& style)
    {
        const auto [u, v] = basisOf(directionOr(normal, {0.f, 1.f, 0.f}));
        const glm::vec3 x = u * size.x * 0.5f, y = v * size.y * 0.5f;
        Quad(center - x - y, center + x - y, center + x + y, center - x + y, style);
    }

    void DebugDraw::Circle(const glm::vec3 center, const glm::vec3 normal, const float radius, const Style& style, const int segments)
    {
        Ring(center, normal, radius, style, segments);
        if (!BeginFill(style)) return;
        const auto [u, v] = basisOf(directionOr(normal, {0.f, 1.f, 0.f}));
        const int count = std::max(segments, 3);
        glm::vec3 previous = center + u * radius;
        for (int i = 1; i <= count; ++i) {
            const float angle = 2.f * pi * static_cast<float>(i) / static_cast<float>(count);
            const glm::vec3 next = center + (u * std::cos(angle) + v * std::sin(angle)) * radius;
            FillTriangle(center, previous, next);
            previous = next;
        }
        EndFill();
    }

    void DebugDraw::Box(const glm::vec3 min, const glm::vec3 max, const Style& style)
    {
        const glm::vec3 center = (min + max) * 0.5f;
        glm::mat4 transform(1.f);
        transform[0].x = max.x - min.x;
        transform[1].y = max.y - min.y;
        transform[2].z = max.z - min.z;
        transform[3] = glm::vec4(center, 1.f);
        Box(transform, style);
    }

    void DebugDraw::Box(const glm::mat4& transform, const Style& style)
    {
        std::array<glm::vec3, 8> corners;
        for (int i = 0; i < 8; ++i) {
            const glm::vec4 local { (i & 1) ? 0.5f : -0.5f, (i & 2) ? 0.5f : -0.5f, (i & 4) ? 0.5f : -0.5f, 1.f };
            corners[i] = glm::vec3(transform * local);
        }
        if (BeginFill(style)) {
            // A face per side of each axis: the corners sharing that axis's bit, around the face.
            for (const int bit : {1, 2, 4}) {
                const int u = bit == 1 ? 2 : 1, v = bit == 4 ? 2 : 4;
                for (const int side : {0, bit}) {
                    FillTriangle(corners[side], corners[side | u], corners[side | u | v]);
                    FillTriangle(corners[side], corners[side | u | v], corners[side | v]);
                }
            }
            EndFill();
        }
        // Each corner joined to the ones differing from it in exactly one axis.
        for (int i = 0; i < 8; ++i)
            for (const int bit : {1, 2, 4})
                if (!(i & bit)) Edge(corners[i], corners[i | bit], style);
    }

    void DebugDraw::Sphere(const glm::vec3 center, const float radius, const Style& style, const int segments)
    {
        Ring(center, {1.f, 0.f, 0.f}, radius, style, segments);
        Ring(center, {0.f, 1.f, 0.f}, radius, style, segments);
        Ring(center, {0.f, 0.f, 1.f}, radius, style, segments);
        if (!BeginFill(style)) return;
        const int around = std::max(segments, 3), rings = std::max(segments / 2, 2);
        const auto at = [&](const int ring, const int step) {
            const float theta = pi * static_cast<float>(ring) / static_cast<float>(rings);
            const float phi = 2.f * pi * static_cast<float>(step) / static_cast<float>(around);
            return center + radius * glm::vec3(std::sin(theta) * std::cos(phi), std::cos(theta), std::sin(theta) * std::sin(phi));
        };
        for (int ring = 0; ring < rings; ++ring)
            for (int step = 0; step < around; ++step) {
                const glm::vec3 a = at(ring, step), b = at(ring + 1, step), c = at(ring + 1, step + 1), d = at(ring, step + 1);
                if (ring > 0) FillTriangle(a, b, d);
                if (ring < rings - 1) FillTriangle(b, c, d);
            }
        EndFill();
    }

    void DebugDraw::Cylinder(const glm::vec3 from, const glm::vec3 to, const float radius, const Style& style, const int segments)
    {
        const glm::vec3 axis = directionOr(to - from, {0.f, 1.f, 0.f});
        const auto [u, v] = basisOf(axis);
        Ring(from, axis, radius, style, segments);
        Ring(to, axis, radius, style, segments);
        for (const glm::vec3 side : {u, v, -u, -v}) Edge(from + side * radius, to + side * radius, style);
        if (!BeginFill(style)) return;
        const int count = std::max(segments, 3);
        const auto rim = [&](const int i) {
            const float angle = 2.f * pi * static_cast<float>(i) / static_cast<float>(count);
            return (u * std::cos(angle) + v * std::sin(angle)) * radius;
        };
        for (int i = 0; i < count; ++i) {
            const glm::vec3 a = rim(i), b = rim(i + 1);
            FillTriangle(from + a, from + b, to + b);
            FillTriangle(from + a, to + b, to + a);
            FillTriangle(from, from + b, from + a);
            FillTriangle(to, to + a, to + b);
        }
        EndFill();
    }

    void DebugDraw::Cone(const glm::vec3 base, const glm::vec3 tip, const float radius, const Style& style, const int segments)
    {
        const glm::vec3 axis = directionOr(tip - base, {0.f, 1.f, 0.f});
        const auto [u, v] = basisOf(axis);
        Ring(base, axis, radius, style, segments);
        for (const glm::vec3 side : {u, v, -u, -v}) Edge(base + side * radius, tip, style);
        if (!BeginFill(style)) return;
        const int count = std::max(segments, 3);
        const auto rim = [&](const int i) {
            const float angle = 2.f * pi * static_cast<float>(i) / static_cast<float>(count);
            return base + (u * std::cos(angle) + v * std::sin(angle)) * radius;
        };
        for (int i = 0; i < count; ++i) {
            const glm::vec3 a = rim(i), b = rim(i + 1);
            FillTriangle(a, b, tip);
            FillTriangle(base, b, a);
        }
        EndFill();
    }

    void DebugDraw::Capsule(const glm::vec3 from, const glm::vec3 to, const float radius, const Style& style, const int segments)
    {
        const glm::vec3 axis = directionOr(to - from, {0.f, 1.f, 0.f});
        const auto [u, v] = basisOf(axis);
        const int count = std::max(segments, 4);
        Ring(from, axis, radius, style, count);
        Ring(to, axis, radius, style, count);
        for (const glm::vec3 side : {u, v, -u, -v}) Edge(from + side * radius, to + side * radius, style);
        // The caps: a half-circle over each end, in two planes.
        const int half = count / 2;
        for (const glm::vec3 side : {u, v}) {
            for (const auto& [end, out] : {std::pair{to, axis}, std::pair{from, -axis}}) {
                glm::vec3 previous = end + side * radius;
                for (int i = 1; i <= half; ++i) {
                    const float angle = pi * static_cast<float>(i) / static_cast<float>(half);
                    const glm::vec3 next = end + (side * std::cos(angle) + out * std::sin(angle)) * radius;
                    Edge(previous, next, style);
                    previous = next;
                }
            }
        }
        if (!BeginFill(style)) return;
        const int rings = std::max(count / 4, 2);
        // A point on the capsule: around the axis by step, and from the cap's rim (0) to its pole (rings).
        const auto at = [&](const glm::vec3 end, const glm::vec3 out, const int ring, const int step) {
            const float theta = 0.5f * pi * static_cast<float>(ring) / static_cast<float>(rings);
            const float phi = 2.f * pi * static_cast<float>(step) / static_cast<float>(count);
            return end + ((u * std::cos(phi) + v * std::sin(phi)) * std::cos(theta) + out * std::sin(theta)) * radius;
        };
        for (int step = 0; step < count; ++step) {
            const glm::vec3 a = at(from, -axis, 0, step), b = at(from, -axis, 0, step + 1);
            const glm::vec3 c = at(to, axis, 0, step + 1), d = at(to, axis, 0, step);
            FillTriangle(a, b, c);
            FillTriangle(a, c, d);
            for (const auto& [end, out] : {std::pair{to, axis}, std::pair{from, -axis}})
                for (int ring = 0; ring < rings; ++ring) {
                    const glm::vec3 p = at(end, out, ring, step), q = at(end, out, ring, step + 1);
                    const glm::vec3 r = at(end, out, ring + 1, step + 1), s = at(end, out, ring + 1, step);
                    FillTriangle(p, q, r);
                    if (ring < rings - 1) FillTriangle(p, r, s);
                }
        }
        EndFill();
    }

    // ---- cameras and lights -----------------------------------------------------------------------

    void DebugDraw::Camera(const glm::mat4& view, const glm::mat4& projection, const float size, const Style& style)
    {
        const glm::mat4 toWorld = glm::inverse(view);
        const glm::mat4 inverse = glm::inverse(projection * view);
        const glm::vec3 eye(toWorld[3]);
        const auto unproject = [&](const float x, const float y, const float z) {
            const glm::vec4 world = inverse * glm::vec4(x, y, z, 1.f);
            return glm::vec3(world) / world.w;
        };
        // Which of clip space's depths is the near one is the projection's to say: the one nearer the eye.
        float nearZ = 0.f, farZ = 1.f;
        if (glm::distance(unproject(0.f, 0.f, 0.f), eye) > glm::distance(unproject(0.f, 0.f, 1.f), eye)) std::swap(nearZ, farZ);
        const glm::vec3 forward = directionOr(unproject(0.f, 0.f, farZ) - unproject(0.f, 0.f, nearZ), -glm::vec3(toWorld[2]));
        const bool perspective = projection[2][3] != 0.f;

        // The corners, in order around the edge, of what it sees at @p size in front of it.
        std::array<glm::vec3, 4> corners, starts;
        constexpr std::array<glm::vec2, 4> ndc {{{-1.f, -1.f}, {1.f, -1.f}, {1.f, 1.f}, {-1.f, 1.f}}};
        for (int i = 0; i < 4; ++i) {
            const glm::vec3 nearPoint = unproject(ndc[i].x, ndc[i].y, nearZ);
            if (perspective) {
                const glm::vec3 ray = directionOr(unproject(ndc[i].x, ndc[i].y, farZ) - eye, forward);
                starts[i] = eye;
                corners[i] = eye + ray * (size / std::max(glm::dot(ray, forward), 1e-3f));
            } else {
                starts[i] = nearPoint;
                corners[i] = nearPoint + forward * size;
            }
        }
        if (BeginFill(style)) {
            for (int i = 0; i < 4; ++i) {
                const int j = (i + 1) % 4;
                FillTriangle(starts[i], corners[i], corners[j]);
                if (!perspective) FillTriangle(starts[i], corners[j], starts[j]);
            }
            EndFill();
        }
        for (int i = 0; i < 4; ++i) {
            const int j = (i + 1) % 4;
            Edge(corners[i], corners[j], style);
            Edge(starts[i], corners[i], style);
            if (!perspective) Edge(starts[i], starts[j], style);
        }

        // Which way is up: a triangle over the edge the camera's own up points to.
        const glm::vec3 up = directionOr(glm::vec3(toWorld[1]), {0.f, 1.f, 0.f});
        const glm::vec3 right = directionOr(glm::vec3(toWorld[0]), {1.f, 0.f, 0.f});
        const glm::vec3 middle = (corners[0] + corners[1] + corners[2] + corners[3]) * 0.25f;
        float height = 0.f, width = 0.f;
        for (const glm::vec3& corner : corners) {
            height = std::max(height, glm::dot(corner - middle, up));
            width = std::max(width, glm::dot(corner - middle, right));
        }
        const float tip = std::min(height, width) * 0.5f;
        const glm::vec3 base = middle + up * (height + tip * 0.2f);
        Triangle(base - right * tip * 0.6f, base + right * tip * 0.6f, base + up * tip, style);
    }

    void DebugDraw::PointLight(const glm::vec3 position, const float range, const Style& style)
    {
        const float size = range > 0.f ? std::min(range * 0.15f, 0.5f) : 0.25f;
        const Style lines = linesOf(style);
        Point(position, size * 2.f, lines);
        // And the corners of a cube: a star, not just a cross.
        for (const glm::vec3 corner : {glm::vec3(1, 1, 1), glm::vec3(1, 1, -1), glm::vec3(1, -1, 1), glm::vec3(-1, 1, 1)}) {
            const glm::vec3 offset = glm::normalize(corner) * size * 0.7f;
            Line(position - offset, position + offset, lines);
        }
        if (range > 0.f) Sphere(position, range, style);
    }

    void DebugDraw::SpotLight(const glm::vec3 position, const glm::vec3 direction, const float range, const float outerAngle,
                              const float innerAngle, const Style& style)
    {
        const glm::vec3 forward = directionOr(direction, {0.f, -1.f, 0.f});
        const float length = range > 0.f ? range : 1.f;
        const float limit = 0.49f * pi;
        const glm::vec3 base = position + forward * length;
        Cone(base, position, length * std::tan(std::clamp(outerAngle, 0.f, limit)), style);
        if (innerAngle > 0.f) {
            Style inner = linesOf(style);
            inner.color.a *= 0.5f;
            Ring(base, forward, length * std::tan(std::clamp(innerAngle, 0.f, limit)), inner, 24);
        }
        Line(position, base, linesOf(style));
    }

    void DebugDraw::DirectionalLight(const glm::vec3 position, const glm::vec3 direction, const float size, const Style& style)
    {
        const glm::vec3 forward = directionOr(direction, {0.f, -1.f, 0.f});
        const auto [u, v] = basisOf(forward);
        const float radius = size * 0.25f;
        Circle(position, forward, radius, style);
        for (const glm::vec3 offset : {glm::vec3(0.f), u * radius, -u * radius, v * radius, -v * radius})
            Arrow(position + offset, position + offset + forward * size, style);
    }

    // ---- gizmos -----------------------------------------------------------------------------------

    namespace {
        constexpr int centerHandle = 6;        // 0-2 are the axes, 3-5 the planes (by their normal's axis)
        constexpr float pickDistance = 8.f;    // pixels

        struct Ray {
            glm::vec3 origin, direction;
        };

        /** Between the world and the pixels of the image a camera draws. */
        struct ScreenSpace {
            glm::mat4 viewProjection, inverse;
            glm::vec2 viewport;

            [[nodiscard]] std::optional<glm::vec2> ToScreen(const glm::vec3 p) const
            {
                const glm::vec4 clip = viewProjection * glm::vec4(p, 1.f);
                if (clip.w <= 1e-6f) return std::nullopt;   // behind the camera
                return glm::vec2((clip.x / clip.w * 0.5f + 0.5f) * viewport.x, (clip.y / clip.w * 0.5f + 0.5f) * viewport.y);
            }
            [[nodiscard]] glm::vec3 Unproject(const glm::vec2 ndc, const float z) const
            {
                const glm::vec4 world = inverse * glm::vec4(ndc, z, 1.f);
                return glm::vec3(world) / world.w;
            }
            /** The line through a pixel. Its direction's sign is the projection's; only the line is used. */
            [[nodiscard]] Ray RayAt(const glm::vec2 pixel) const
            {
                const glm::vec2 ndc = pixel / viewport * 2.f - 1.f;
                const glm::vec3 a = Unproject(ndc, 0.f), b = Unproject(ndc, 1.f);
                return {a, directionOr(b - a, {0.f, 0.f, -1.f})};
            }
            /** How long a pixel is in the world, at @p p. */
            [[nodiscard]] float WorldPerPixel(const glm::vec3 p) const
            {
                const glm::vec4 clip = viewProjection * glm::vec4(p, 1.f);
                if (clip.w <= 1e-6f) return 0.f;
                const glm::vec3 ndc = glm::vec3(clip) / clip.w;
                return glm::distance(Unproject({ndc.x + 2.f / viewport.x, ndc.y}, ndc.z), p);
            }
        };

        float distanceToSegment(const glm::vec2 p, const glm::vec2 a, const glm::vec2 b)
        {
            const glm::vec2 ab = b - a;
            const float lengthSquared = glm::dot(ab, ab);
            const float t = lengthSquared > 0.f ? std::clamp(glm::dot(p - a, ab) / lengthSquared, 0.f, 1.f) : 0.f;
            return glm::distance(p, a + ab * t);
        }

        bool insideConvex(const glm::vec2 p, const std::array<glm::vec2, 4>& corners)
        {
            float sign = 0.f;
            for (int i = 0; i < 4; ++i) {
                const glm::vec2 edge = corners[(i + 1) % 4] - corners[i], to = p - corners[i];
                const float cross = edge.x * to.y - edge.y * to.x;
                if (cross == 0.f) continue;
                if (sign == 0.f) sign = cross;
                else if ((cross > 0.f) != (sign > 0.f)) return false;
            }
            return true;
        }

        /** Where along the line @p origin + @p axis * t the ray passes closest; none when they run parallel. */
        std::optional<float> paramAlong(const glm::vec3 origin, const glm::vec3 axis, const Ray& ray)
        {
            const float b = glm::dot(axis, ray.direction);
            const float denominator = 1.f - b * b;
            if (denominator < 1e-6f) return std::nullopt;
            const glm::vec3 w = origin - ray.origin;
            return (b * glm::dot(ray.direction, w) - glm::dot(axis, w)) / denominator;
        }

        std::optional<glm::vec3> hitPlane(const glm::vec3 origin, const glm::vec3 normal, const Ray& ray)
        {
            const float denominator = glm::dot(normal, ray.direction);
            if (std::abs(denominator) < 1e-5f) return std::nullopt;
            return ray.origin + ray.direction * (glm::dot(origin - ray.origin, normal) / denominator);
        }

        float angleOf(const glm::vec2 v) { return std::atan2(v.y, v.x); }
        float wrapAngle(float a)
        {
            while (a > pi) a -= 2.f * pi;
            while (a < -pi) a += 2.f * pi;
            return a;
        }
        float snapped(const float value, const float step) { return step > 0.f ? std::round(value / step) * step : value; }

        /** The gizmo's axes: the world's, or @p transform's own with its scale taken out. */
        std::array<glm::vec3, 3> axesOf(const glm::mat4& transform, const bool local)
        {
            std::array<glm::vec3, 3> axes {glm::vec3(1.f, 0.f, 0.f), glm::vec3(0.f, 1.f, 0.f), glm::vec3(0.f, 0.f, 1.f)};
            if (local)
                for (int i = 0; i < 3; ++i) axes[i] = directionOr(glm::vec3(transform[i]), axes[i]);
            return axes;
        }

        constexpr std::array<glm::vec4, 3> axisColors {{{0.95f, 0.25f, 0.3f, 1.f}, {0.45f, 0.85f, 0.2f, 1.f}, {0.25f, 0.5f, 1.f, 1.f}}};
        constexpr glm::vec4 hotColor {1.f, 0.85f, 0.15f, 1.f};
        constexpr glm::vec4 centerColor {0.95f, 0.95f, 0.95f, 1.f};

        DebugStyle handleStyle(const glm::vec4 color, const float fillAlpha = 0.f)
        {
            return {.color = color, .onTop = true, .fill = fillAlpha > 0.f ? glm::vec4(glm::vec3(color), fillAlpha) : glm::vec4(0.f),
                    .lineWidth = 2.f};
        }
    }

    bool DebugDraw::Gizmo(const GizmoMode mode, glm::mat4& transform, const glm::mat4& viewProjection, const GizmoPointer& pointer,
                          const GizmoOptions& given, const std::uint64_t id)
    {
        // Its sizes are on screen, in points: on a scaled display (Retina, two pixels to a point) the image the
        // camera draws is drawn at the display's density, so a pixel of it is less than a point. As big, and as
        // easy to take hold of, as anywhere else — and nothing changes where a pixel is a point.
        float density = 1.f;
        if (const Scene* scene = Scene::Current(); scene && !scene->SceneWindow().IsOffscreen())
            density = std::max(scene->SceneWindow().PixelRatio(), 1.f);
        GizmoOptions options = given;
        options.size *= density;
        const float pick = pickDistance * density;
        const auto handleStyle = [density](const glm::vec4 color, const float fillAlpha = 0.f) {
            DebugStyle style = kor::handleStyle(color, fillAlpha);
            style.lineWidth *= density;
            return style;
        };
        // Ids given and ids by order kept apart: the top bit is the order's.
        const std::uint64_t key = id != 0 ? (id & ~(std::uint64_t(1) << 63)) : ((std::uint64_t(1) << 63) | _gizmoCalls);
        ++_gizmoCalls;
        bool dragging = _drag && _drag->key == key;
        if (dragging && _drag->mode != mode) {
            _drag.reset();
            dragging = false;
        }
        if (dragging) _drag->seen = true;
        if (pointer.viewport.x <= 0.f || pointer.viewport.y <= 0.f) return false;

        const ScreenSpace screen {viewProjection, glm::inverse(viewProjection), pointer.viewport};
        const auto originOnScreen = screen.ToScreen(glm::vec3(transform[3]));
        if (!originOnScreen) {
            if (dragging) _drag.reset();
            return false;
        }

        const bool local = mode == GizmoMode::eScale || options.space == GizmoSpace::eLocal;
        // Where the handles are, how big — the same size on screen however far away — and which way the
        // camera looks at them.
        glm::vec3 origin(transform[3]), view = screen.RayAt(*originOnScreen).direction;
        float size = screen.WorldPerPixel(origin) * options.size;
        std::array<glm::vec3, 3> axes = axesOf(transform, local);
        if (!(size > 0.f)) return false;

        // Handles seen end-on — an axis pointing at the camera, a plane edge-on — would only jump.
        std::array<bool, 3> axisShown {}, planeShown {};
        for (int i = 0; i < 3; ++i) {
            const auto tip = screen.ToScreen(origin + axes[i] * size);
            axisShown[i] = tip && glm::distance(*tip, *originOnScreen) > options.size * 0.15f;
            planeShown[i] = std::abs(glm::dot(axes[i], view)) > 0.25f;
        }
        const auto planeCorners = [&](const int normal, const glm::vec3 at, const std::array<glm::vec3, 3>& basis, const float s) {
            const glm::vec3 u = basis[(normal + 1) % 3], v = basis[(normal + 2) % 3];
            return std::array<glm::vec3, 4> {at + (u * 0.2f + v * 0.2f) * s, at + (u * 0.45f + v * 0.2f) * s,
                                             at + (u * 0.45f + v * 0.45f) * s, at + (u * 0.2f + v * 0.45f) * s};
        };
        const auto ringPoint = [](const glm::vec3 center, const glm::vec3 normal, const float radius, const float angle) {
            const auto [u, v] = basisOf(normal);
            return center + (u * std::cos(angle) + v * std::sin(angle)) * radius;
        };
        constexpr int ringSegments = 64;
        constexpr float screenRing = 1.2f;   // the screen ring's radius, in the gizmo's size

        // ---- which handle the pointer is over ----
        int hot = dragging ? _drag->handle : -1;
        glm::vec3 hotDirection(0.f);   // for a ring: where on it the pointer is
        if (!dragging && pointer.position) {
            const glm::vec2 cursor = *pointer.position;
            float best = pick;
            const auto consider = [&](const int handle, const float distance) {
                if (distance < best) {
                    best = distance;
                    hot = handle;
                }
            };
            const auto segmentDistance = [&](const glm::vec3 a, const glm::vec3 b) {
                const auto sa = screen.ToScreen(a), sb = screen.ToScreen(b);
                return sa && sb ? distanceToSegment(cursor, *sa, *sb) : std::numeric_limits<float>::max();
            };
            const float toCenter = glm::distance(cursor, *originOnScreen);
            if (mode == GizmoMode::eRotate) {
                for (int handle : {0, 1, 2, centerHandle}) {
                    const glm::vec3 normal = handle == centerHandle ? view : axes[handle];
                    const float radius = handle == centerHandle ? size * screenRing : size;
                    glm::vec3 previous = ringPoint(origin, normal, radius, 0.f);
                    for (int i = 1; i <= ringSegments; ++i) {
                        const float angle = 2.f * pi * static_cast<float>(i) / ringSegments;
                        const glm::vec3 next = ringPoint(origin, normal, radius, angle);
                        const float distance = segmentDistance(previous, next);
                        if (distance < best) hotDirection = glm::normalize(next - origin);
                        consider(handle, distance);
                        previous = next;
                    }
                }
            } else {
                if (toCenter < (mode == GizmoMode::eScale ? 12.f : 10.f) * density) consider(centerHandle, 0.f);
                for (int i = 0; i < 3; ++i)
                    if (axisShown[i]) consider(i, segmentDistance(origin + axes[i] * size * 0.15f, origin + axes[i] * size));
                if (hot < 0 && mode == GizmoMode::eTranslate) {
                    for (int i = 0; i < 3 && hot < 0; ++i) {
                        if (!planeShown[i]) continue;
                        std::array<glm::vec2, 4> corners;
                        bool all = true;
                        const auto world = planeCorners(i, origin, axes, size);
                        for (int c = 0; c < 4; ++c) {
                            const auto s = screen.ToScreen(world[c]);
                            all = all && s.has_value();
                            if (s) corners[c] = *s;
                        }
                        if (all && insideConvex(cursor, corners)) hot = 3 + i;
                    }
                }
            }
        }
        if (hot >= 0) _hovered = true;

        // ---- grabbing it ----
        if (!dragging && hot >= 0 && pointer.pressed && pointer.position) {
            const glm::vec2 cursor = *pointer.position;
            const Ray ray = screen.RayAt(cursor);
            GizmoDrag drag {key, mode, hot, transform, {}, {}, 0.f, cursor, 0.f, angleOf(cursor - *originOnScreen), 1.f, true};
            bool grabbed = true;
            if (mode == GizmoMode::eRotate) {
                drag.axis = hot == centerHandle ? view : axes[hot];
                drag.grab = hotDirection;
                // Which way a turn on screen turns around the axis: tried on screen, whatever the projection.
                float best = 0.f;
                const auto [u, v] = basisOf(drag.axis);
                for (const glm::vec3 side : {u, v}) {
                    const auto a = screen.ToScreen(origin + side * size);
                    const auto b = screen.ToScreen(origin + glm::vec3(glm::rotate(glm::mat4(1.f), 0.1f, drag.axis) * glm::vec4(side, 0.f)) * size);
                    if (!a || !b) continue;
                    const float turned = wrapAngle(angleOf(*b - *originOnScreen) - angleOf(*a - *originOnScreen));
                    if (std::abs(turned) > std::abs(best)) best = turned;
                }
                drag.screenSign = best < 0.f ? -1.f : 1.f;
            } else if (hot < 3) {
                drag.axis = axes[hot];
                const auto param = paramAlong(origin, drag.axis, ray);
                grabbed = param.has_value() && (mode != GizmoMode::eScale || std::abs(*param) > size * 1e-3f);
                drag.grabParam = param.value_or(0.f);
            } else if (mode == GizmoMode::eTranslate) {
                drag.axis = hot == centerHandle ? view : axes[hot - 3];
                const auto hit = hitPlane(origin, drag.axis, ray);
                grabbed = hit.has_value();
                drag.grab = hit.value_or(origin);
            }
            if (grabbed) {
                _drag = drag;
                dragging = true;
            }
        }

        // ---- dragging it ----
        bool changed = false;
        float scaleShown[4] {1.f, 1.f, 1.f, 1.f};   // how long each scale handle is drawn: per axis, and all
        if (dragging && !pointer.down) {
            _drag.reset();
            dragging = false;
        } else if (dragging && pointer.position) {
            GizmoDrag& drag = *_drag;
            const glm::vec2 cursor = *pointer.position;
            const Ray ray = screen.RayAt(cursor);
            const glm::vec3 start(drag.start[3]);
            const auto startAxes = axesOf(drag.start, local);
            glm::mat4 result = drag.start;
            if (mode == GizmoMode::eTranslate) {
                glm::vec3 delta(0.f);
                if (drag.handle < 3) {
                    if (const auto param = paramAlong(start, drag.axis, ray))
                        delta = drag.axis * snapped(*param - drag.grabParam, options.snap);
                } else if (const auto hit = hitPlane(start, drag.axis, ray)) {
                    delta = *hit - drag.grab;
                    if (options.snap > 0.f)
                        for (const glm::vec3& axis : startAxes) {
                            const float along = glm::dot(delta, axis);
                            delta += axis * (snapped(along, options.snap) - along);
                        }
                }
                result[3] += glm::vec4(delta, 0.f);
            } else if (mode == GizmoMode::eRotate) {
                const float angle = angleOf(cursor - *originOnScreen);
                drag.angle += wrapAngle(angle - drag.lastCursorAngle);
                drag.lastCursorAngle = angle;
                const float turn = snapped(drag.angle * drag.screenSign, glm::radians(options.snap));
                result = glm::translate(glm::mat4(1.f), start) * glm::rotate(glm::mat4(1.f), turn, drag.axis)
                       * glm::translate(glm::mat4(1.f), -start) * drag.start;
            } else {
                float factor = 1.f;
                if (drag.handle < 3) {
                    if (const auto param = paramAlong(start, drag.axis, ray)) factor = *param / drag.grabParam;
                } else {
                    const glm::vec2 moved = cursor - drag.grabCursor;
                    factor = 1.f + (moved.x - moved.y) / options.size;
                }
                factor = std::max(1.f + snapped(factor - 1.f, options.snap), 1e-3f);
                for (int i = 0; i < 3; ++i)
                    if (drag.handle == i || drag.handle == centerHandle) result[i] *= factor;
                scaleShown[drag.handle < 3 ? drag.handle : 3] = factor;
            }
            changed = result != transform;
            transform = result;
            origin = glm::vec3(transform[3]);
            axes = axesOf(transform, local);
            if (const auto moved = screen.ToScreen(origin)) {
                view = screen.RayAt(*moved).direction;
                size = std::max(screen.WorldPerPixel(origin) * options.size, 0.f);
            }
        }

        // ---- drawing it ----
        const auto colorOf = [&](const int handle, const glm::vec4 color) { return handle == hot ? hotColor : color; };
        if (mode == GizmoMode::eTranslate) {
            if (dragging) Line(glm::vec3(_drag->start[3]), origin, handleStyle({0.6f, 0.6f, 0.6f, 1.f}));
            for (int i = 0; i < 3; ++i) {
                if (!planeShown[i]) continue;
                const auto c = planeCorners(i, origin, axes, size);
                Quad(c[0], c[1], c[2], c[3], handleStyle(colorOf(3 + i, axisColors[i]), hot == 3 + i ? 0.6f : 0.3f));
            }
            for (int i = 0; i < 3; ++i) {
                if (!axisShown[i]) continue;
                const glm::vec4 color = colorOf(i, axisColors[i]);
                Line(origin, origin + axes[i] * size * 0.8f, handleStyle(color));
                Style head = handleStyle(color, 1.f);
                head.outline = false;
                Cone(origin + axes[i] * size * 0.78f, origin + axes[i] * size, size * 0.06f, head, 12);
            }
            Circle(origin, view, size * 0.08f, handleStyle(colorOf(centerHandle, centerColor)), 16);
        } else if (mode == GizmoMode::eRotate) {
            for (int i = 0; i < 3; ++i) Circle(origin, axes[i], size, handleStyle(colorOf(i, axisColors[i])), ringSegments);
            Circle(origin, view, size * screenRing, handleStyle(colorOf(centerHandle, {0.8f, 0.8f, 0.8f, 1.f})), ringSegments);
            if (dragging) {
                // What it has turned through, as a slice.
                const float turn = snapped(_drag->angle * _drag->screenSign, glm::radians(options.snap));
                const float radius = _drag->handle == centerHandle ? size * screenRing : size;
                const glm::vec3 from = glm::normalize(_drag->grab - _drag->axis * glm::dot(_drag->grab, _drag->axis)) * radius;
                const int steps = std::clamp(static_cast<int>(std::abs(turn) / (2.f * pi) * ringSegments) + 1, 1, ringSegments * 4);
                Style slice = handleStyle(hotColor, 0.25f);
                slice.outline = false;
                if (BeginFill(slice)) {
                    glm::vec3 previous = origin + from;
                    for (int i = 1; i <= steps; ++i) {
                        const float a = turn * static_cast<float>(i) / static_cast<float>(steps);
                        const glm::vec3 next = origin + glm::vec3(glm::rotate(glm::mat4(1.f), a, _drag->axis) * glm::vec4(from, 0.f));
                        FillTriangle(origin, previous, next);
                        previous = next;
                    }
                    EndFill();
                }
                Line(origin, origin + from, handleStyle(hotColor));
                Line(origin, origin + glm::vec3(glm::rotate(glm::mat4(1.f), turn, _drag->axis) * glm::vec4(from, 0.f)), handleStyle(hotColor));
            }
        } else {
            for (int i = 0; i < 3; ++i) {
                if (!axisShown[i]) continue;
                const glm::vec4 color = colorOf(i, axisColors[i]);
                const glm::vec3 tip = origin + axes[i] * size * scaleShown[i] * scaleShown[3];
                Line(origin, tip, handleStyle(color));
                glm::mat4 cube(1.f);
                for (int c = 0; c < 3; ++c) cube[c] = glm::vec4(axes[c] * size * 0.1f, 0.f);
                cube[3] = glm::vec4(tip, 1.f);
                Box(cube, handleStyle(color, 1.f));
            }
            glm::mat4 cube(1.f);
            for (int c = 0; c < 3; ++c) cube[c] = glm::vec4(axes[c] * size * 0.15f, 0.f);
            cube[3] = glm::vec4(origin, 1.f);
            Box(cube, handleStyle(colorOf(centerHandle, centerColor), 0.8f));
        }
        return changed;
    }

    // ---- the frame --------------------------------------------------------------------------------

    void DebugDraw::Clear()
    {
        _lines.clear();
        _solidVertices.clear();
        _solids.clear();
    }

    void DebugDraw::NextFrame(const float frameTime)
    {
        EndFrame(frameTime);
        BeginFrame(_frame + 1);
    }

    void DebugDraw::BeginFrame(const std::uint64_t frame)
    {
        _frame = frame;
        _gizmoCalls = 0;
        _hoveredBefore = _hovered;
        _hovered = false;
        // A gizmo no longer drawn — what it moved deselected — lets go of its drag.
        if (_drag) {
            if (!_drag->seen) _drag.reset();
            else _drag->seen = false;
        }
    }

    void DebugDraw::EndFrame(const float frameTime)
    {
        std::erase_if(_lines, [&](Stored& line) {
            line.remaining -= frameTime;
            return line.remaining <= 0.f;
        });
        // The fills that stay, moved down over the ones that went.
        std::vector<glm::vec3> kept;
        std::erase_if(_solids, [&](Solid& solid) {
            solid.remaining -= frameTime;
            if (solid.remaining <= 0.f) return true;
            const auto first = static_cast<std::uint32_t>(kept.size());
            kept.insert(kept.end(), _solidVertices.begin() + solid.first, _solidVertices.begin() + solid.first + solid.count);
            solid.first = first;
            return false;
        });
        _solidVertices = std::move(kept);
    }

    // ---- drawing ----------------------------------------------------------------------------------

    namespace {
        std::string formatsOf(const Framebuffer& target)
        {
            std::string key;
            for (glm::u32 i = 0; i < target.ColorAttachmentCount(); ++i)
                key += std::format("c{},", static_cast<int>(target.ColorImage(i)->PixelFormat()));
            if (const auto depth = target.DepthAttachment(); depth.Valid())
                key += std::format("d{}", static_cast<int>(depth->SourceImage()->PixelFormat()));
            return key;
        }
    }

    void DebugDraw::Prepare(const ResourceRef<const Framebuffer>& target, const glm::mat4& viewProjection)
    {
        // The shapes, once a frame however many passes draw them, in the order they are drawn: opaque
        // fills, lines, see-through fills back to front, then what goes on top of everything.
        if (_uploaded != _frame) {
            _uploaded = _frame;
            std::vector<Vertex> vertices;
            vertices.reserve(_lines.size() * 2 + _solidVertices.size());

            const bool perspective = viewProjection[0][3] != 0.f || viewProjection[1][3] != 0.f || viewProjection[2][3] != 0.f;
            const auto depthOf = [&](const glm::vec3 p) {
                const glm::vec4 clip = viewProjection * glm::vec4(p, 1.f);
                return perspective ? clip.w : clip.z;
            };
            std::vector<std::pair<float, const Solid*>> opaque, seeThrough, onTop;
            for (const Solid& solid : _solids)
                (solid.onTop ? onTop : solid.color.a >= 1.f ? opaque : seeThrough).emplace_back(depthOf(solid.center), &solid);
            const auto farthestFirst = [](const auto& a, const auto& b) { return a.first > b.first; };
            std::ranges::stable_sort(seeThrough, farthestFirst);
            std::ranges::stable_sort(onTop, farthestFirst);

            const auto addFills = [&](const std::vector<std::pair<float, const Solid*>>& solids) {
                for (const auto& [depth, solid] : solids)
                    for (std::uint32_t i = solid->first; i < solid->first + solid->count; ++i)
                        vertices.push_back({glm::vec4(_solidVertices[i], 1.f), solid->color});
            };
            // A wide line's width rides in its first end's w, which the wide-line shader reads it from.
            const auto addLines = [&](const bool onTopLines, const bool wide) {
                for (const auto& line : _lines) {
                    if (line.onTop != onTopLines || (line.width > 1.f) != wide) continue;
                    vertices.push_back({glm::vec4(line.from, wide ? line.width : 1.f), line.color});
                    vertices.push_back({glm::vec4(line.to, 1.f), line.color});
                }
            };
            const std::array<std::function<void()>, eBatchCount> batches {
                [&] { addFills(opaque); }, [&] { addLines(false, false); }, [&] { addLines(false, true); },
                [&] { addFills(seeThrough); }, [&] { addFills(onTop); }, [&] { addLines(true, false); },
                [&] { addLines(true, true); }};
            for (int batch = 0; batch < eBatchCount; ++batch) {
                const std::size_t before = vertices.size();
                batches[batch]();
                _counts[batch] = static_cast<std::uint32_t>(vertices.size() - before);
            }

            if (vertices.size() > _capacity) {
                _capacity = std::max<std::size_t>(vertices.size() * 2, 1024);
                _buffer = Buffer::RawBuilder{}
                    .SetRawSize(static_cast<glm::i64>(_capacity * sizeof(Vertex)))
                    .SetUsage(Buffer::Usage::eStorage)
                    .SetType(Buffer::Type::eDynamic)
                    .SetIsPerFrame(true)
                    .Build();
                _buffer.SetName("debug shapes");
            }
            if (!vertices.empty() && _buffer.Valid()) _buffer->Write(std::span<const Vertex>(vertices), 0);
        }
        if (!target.Valid() || !_buffer.Valid()) return;

        // A pipeline per batch for what the target is made of — tested against its depth when it has
        // one — and a set for each, written for the buffer as it now is.
        auto& entry = _targets[formatsOf(*target)];
        if (!entry.pipelines[0].Valid() && !entry.pipelines[0].Poisoned()) {
            const auto vertex = Shader::Builder{}.SetPath(ShaderPath("koralDebugLines.vert.glsl")).GetOrBuild();
            const auto wideVertex = Shader::Builder{}.SetPath(ShaderPath("koralDebugWideLines.vert.glsl")).GetOrBuild();
            const auto fragment = Shader::Builder{}.SetPath(ShaderPath("koralDebugLines.frag.glsl")).GetOrBuild();
            const bool hasDepth = target->DepthAttachment().Valid();
            ColorBlendState blend;
            blend.attachments.resize(target->ColorAttachmentCount(), ColorBlendState::AttachmentState{
                .blendEnable = true, .srcColorBlendFactor = BlendFactor::eSrcAlpha, .dstColorBlendFactor = BlendFactor::eOneMinusSrcAlpha});
            const auto make = [&](const bool triangles, const bool depthTest, const bool depthWrite, const bool wide = false) {
                RasterizationState rasterization;
                // Fills pushed a little behind their own outlines, so the outline is not lost in them.
                if (triangles && !wide) {
                    rasterization.depthBiasEnable = true;
                    rasterization.depthBiasConstantFactor = 1.f;
                    rasterization.depthBiasSlopeFactor = 1.f;
                }
                return GraphicsPipeline::Builder()
                    .SetVertexShader(wide ? wideVertex : vertex)
                    .SetFragmentShader(fragment)
                    .SetFramebuffer(target)
                    .SetInputAssemblyState({.topology = triangles || wide ? Topology::eTriangleList : Topology::eLineList})
                    .SetRasterizationState(rasterization)
                    .SetDepthStencilState({.depthTestEnable = depthTest && hasDepth, .depthWriteEnable = depthWrite && hasDepth,
                                           .depthCompareOp = CompareOp::eLessOrEqual})
                    .SetColorBlendState(blend)
                    .Build();
            };
            entry.pipelines[eOpaqueFills] = make(true, true, true);
            entry.pipelines[eLines] = make(false, true, false);
            entry.pipelines[eWideLines] = make(false, true, false, true);
            entry.pipelines[eSeeThroughFills] = make(true, true, false);
            entry.pipelines[eFillsOnTop] = make(true, false, false);
            entry.pipelines[eLinesOnTop] = make(false, false, false);
            entry.pipelines[eWideLinesOnTop] = make(false, false, false, true);
            for (const auto& pipeline : entry.pipelines)
                if (!pipeline.Valid()) {
                    log::Error("[debug draw] a pipeline could not be made: {}",
                               pipeline.Failure() ? pipeline.Failure()->message : "no reason given");
                    break;
                }
        }
        const bool allValid = std::ranges::all_of(entry.pipelines, [](const auto& p) { return p.Valid(); });
        if (entry.buffer != _buffer.Get() && allValid) {
            entry.buffer = _buffer.Get();
            for (int batch = 0; batch < eBatchCount; ++batch)
                entry.sets[batch] = DescriptorSet::Builder(ResourceRef<const GraphicsPipeline>(entry.pipelines[batch]), 0).Write(0, _buffer).Build();
        }
    }

    void DebugDraw::Record(CommandBuffer& commandBuffer, const glm::mat4& viewProjection, const ResourceRef<const Framebuffer>& target) const
    {
        if (std::ranges::all_of(_counts, [](const std::uint32_t c) { return c == 0; }) || !target.Valid()) return;
        const auto it = _targets.find(formatsOf(*target));
        if (it == _targets.end() || !std::ranges::all_of(it->second.sets, [](const auto& s) { return s.Valid(); })) return;
        const auto& entry = it->second;

        commandBuffer.BeginRendering(RenderInfo(target)
            .SetColorLoadOperation(LoadOperation::eLoad)
            .SetDepthLoadOperation(LoadOperation::eLoad)
            .SetStencilLoadOperation(LoadOperation::eLoad));
        const glm::vec2 viewport(target->Extent());
        std::uint32_t first = 0;
        for (int batch = 0; batch < eBatchCount; ++batch) {
            if (_counts[batch] > 0) {
                commandBuffer.BindGraphicsPipeline(entry.pipelines[batch]).BindDescriptorSet(0, entry.sets[batch])
                    .PushConstant("viewProjection", viewProjection);
                if (batch == eWideLines || batch == eWideLinesOnTop) {
                    // Six vertices a line, made in the shader from the two it pulls from the buffer.
                    commandBuffer.PushConstant("viewport", viewport).PushConstant("firstVertex", first)
                        .Draw(_counts[batch] / 2 * 6, 1, 0);
                } else {
                    commandBuffer.Draw(_counts[batch], 1, first);
                }
            }
            first += _counts[batch];
        }
        commandBuffer.EndRendering();
    }

    void DebugDraw::Render(CommandBuffer& commandBuffer, const glm::mat4& viewProjection, const ResourceRef<const Framebuffer> target)
    {
        Prepare(target, viewProjection);
        Record(commandBuffer, viewProjection, target);
    }

    // ---- as a pass --------------------------------------------------------------------------------

    DebugDrawPass::DebugDrawPass(DebugDraw& draw, std::function<glm::mat4()> viewProjection, std::string target, std::string depth)
        : RenderPass("Debug lines"), _draw(draw), _viewProjection(std::move(viewProjection)),
          _target(std::move(target)), _depth(std::move(depth)) {}

    void DebugDrawPass::Setup(PassBuilder& builder)
    {
        builder.Write(_target, Image::Usage::eColorAttachment);
        if (!_depth.empty()) builder.Write(_depth, Image::Usage::eDepthStencilAttachment);
    }

    void DebugDrawPass::Initialize(const PassResources& resources)
    {
        auto framebuffer = Framebuffer::Builder().AddColor({ .view = resources.ImageNamed(_target) });
        if (!_depth.empty()) {
            const auto depth = resources.ImageNamed(_depth);
            if (depth.Valid() && IsStencilFormat(depth->PixelFormat())) framebuffer.SetDepthStencil({ .view = depth });
            else framebuffer.SetDepth({ .view = depth });
        }
        _framebuffer = framebuffer.Build();
    }

    void DebugDrawPass::Prepare()
    {
        _matrix = _viewProjection ? _viewProjection() : glm::mat4(1.f);
        _draw.Prepare(_framebuffer, _matrix);
    }

    void DebugDrawPass::Record(CommandBuffer& commandBuffer) const
    {
        _draw.Record(commandBuffer, _matrix, _framebuffer);
    }
}
