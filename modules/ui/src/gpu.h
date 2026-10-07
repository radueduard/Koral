//
// koral-ui: the GPU's view of a frame. Mirrors shaders/koralUICommon.glsl — change both together.
//

#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include <kmath/matrix.h>

#include <image.h>
#include <resource.h>

#include <kui/canvas.h>

namespace kui::detail
{
    enum Kind : std::uint32_t {
        eRect = 0, eEllipse = 1, eArc = 2, eSegment = 3, eTriangle = 4, eBezier = 5,
        eGlyph = 6, eImage = 7, eShadow = 8, eMesh = 9, eCustom = 10, eBackdrop = 11,
    };
    enum Flag : std::uint32_t { eFill = 1, eStroke = 2, eGradient = 4 };

    inline constexpr std::uint32_t None = 0xffff;

    struct Instance {
        kor::Vec4 bounds {};
        kor::Vec4 xform { 1.f, 0.f, 0.f, 1.f };
        kor::Vec2 translate {};
        std::uint32_t layerClip = None;     ///< layer << 16 | clip
        std::uint32_t kindFlags = 0;        ///< texture << 16 | flags << 8 | kind
        kor::Vec4 shape0 {};
        kor::Vec4 shape1 {};
        std::uint32_t fill = 0;
        std::uint32_t stroke = 0;
        float strokeWidth = 0.f;
        std::uint32_t paint = 0xffffffffu;

        [[nodiscard]] std::uint32_t Kind() const { return kindFlags & 0xffu; }
        [[nodiscard]] std::uint32_t Flags() const { return (kindFlags >> 8) & 0xffu; }
        [[nodiscard]] std::uint32_t Texture() const { return kindFlags >> 16; }
        void SetTexture(const std::uint32_t texture) { kindFlags = (kindFlags & 0xffffu) | (texture << 16); }
        [[nodiscard]] std::uint32_t Clip() const { return layerClip & 0xffffu; }
        void SetLayerClip(const std::uint32_t layer, const std::uint32_t clip) { layerClip = (layer << 16) | (clip & 0xffffu); }
    };
    static_assert(sizeof(Instance) == 96);

    struct Vertex {
        kor::Vec2 position {};
        float coverage = 1.f;
        std::uint32_t instance = 0;
    };
    static_assert(sizeof(Vertex) == 16);

    struct GpuLayer {
        kor::Vec4 m { 1.f, 0.f, 0.f, 1.f };
        kor::Vec2 t {};
        float opacity = 1.f;
        std::uint32_t pad = 0;
    };
    static_assert(sizeof(GpuLayer) == 32);

    struct GpuClip {
        kor::Vec4 m { 1.f, 0.f, 0.f, 1.f };
        kor::Vec2 t {};
        std::uint32_t parent = None;
        std::uint32_t pad = 0;
        kor::Vec4 rect {};
        kor::Vec4 radii {};
    };
    static_assert(sizeof(GpuClip) == 64);

    struct GpuGradient {
        kor::Vec4 geometry {};
        std::uint32_t type = 0, count = 0, pad0 = 0, pad1 = 0;
        std::uint32_t colors[8] {};
        float stops[8] {};
    };
    static_assert(sizeof(GpuGradient) == 96);

    inline kor::Vec4 Pack(const Transform& t) { return { t.a, t.b, t.c, t.d }; }
}

namespace kui
{
    /** @brief What a picture holds: instances in its own space, with picture-local indices. */
    struct Picture::Data {
        struct Clip {
            Transform transform;            ///< clip local -> picture
            Rect rect;
            Radii radii;
            std::uint32_t parent = detail::None;
        };
        struct LayerRef {
            std::shared_ptr<Layer> layer;
            Transform transform;            ///< where it was drawn, in picture space
            std::uint32_t clip = detail::None;
        };
        /** @brief A stretch of the picture drawn one way, in order. */
        struct Run {
            enum class Kind : std::uint8_t { ePrimitives, eMesh, eElement, eLayer, eBackdrop };   // eBackdrop: primitives that show what is behind them
            Kind kind = Kind::ePrimitives;
            std::uint32_t first = 0, count = 0;   ///< Instances (primitives, elements), vertices (mesh), or the LayerRef.
            std::shared_ptr<ElementShader> shader;
        };

        std::vector<detail::Instance> instances;
        std::vector<detail::Vertex> vertices;
        std::vector<Clip> clips;
        std::vector<detail::GpuGradient> gradients;
        std::vector<Run> runs;
        std::vector<LayerRef> layers;
        std::vector<kor::ResourceRef<const kor::Image>> textures;   ///< by picture-local slot; slot 0 is the glyph atlas
        std::vector<std::vector<std::byte>> parameters;             ///< by element: what Instance::paint names
        Rect bounds;
    };
}
