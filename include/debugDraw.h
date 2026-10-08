//
// Shapes drawn for a frame, or a few seconds: what a scene draws to see what it is doing — and the
// gizmos that move, turn and scale what it shows.
//

#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include <kmath/matrix.h>

#include "api.h"
#include "buffer.h"
#include "descriptorSet.h"
#include "frameGraph.h"
#include "framebuffer.h"
#include "graphicsPipeline.h"
#include "resource.h"

namespace kor
{
    class App;

    /**
     * @brief How a debug shape is drawn: its outline, its fill, or both. @see DebugDraw
     *
     * @code
     * Debug::Box(min, max);                                                       // white outline
     * Debug::Box(min, max, {.fill = {0, 1, 0, 1}, .outline = false});             // solid green
     * Debug::Sphere(c, r, {.color = {1, 0, 0, 1}, .fill = {1, 0, 0, 0.2f}});      // see-through red, outlined
     * @endcode
     */
    struct DebugStyle {
        /** The outline's colour, and a line's. */
        kor::Vec4 color { 1.f, 1.f, 1.f, 1.f };
        /** Seconds of the scene's time to keep drawing it; 0 is this frame only. */
        float duration = 0.f;
        /** Drawn over everything, rather than hidden by what is in front of it. */
        bool onTop = false;
        /**
         * The colour to fill the shape with; alpha below 1 is a fill you can see through, and an alpha of
         * 0 — the default — no fill at all. Lines, points, grids and frustums have nothing to fill.
         */
        kor::Vec4 fill { 0.f };
        /** Draws the outline; false for a shape that is only its fill. */
        bool outline = true;
        /**
         * How wide its lines are, in pixels, however near or far. Wider than 1, a line is a quad facing
         * the screen, so any width works on any device.
         */
        float lineWidth = 1.f;
    };

    /** @brief What a gizmo does to the transform it is given. @see DebugDraw::Gizmo */
    enum class GizmoMode : std::uint8_t { eTranslate, eRotate, eScale };

    /** @brief Whether a gizmo's handles follow the world's axes or the transform's own. Scaling is always the transform's own. */
    enum class GizmoSpace : std::uint8_t { eWorld, eLocal };

    /** @brief The pointer a gizmo is used with, in the pixels of the image its camera draws. @see DebugDraw::Gizmo */
    struct GizmoPointer {
        /** Where it is, from the image's top-left; none when it is not over the image (or something else has it). */
        std::optional<kor::Vec2> position;
        /** The size of the image, in pixels. */
        kor::Vec2 viewport { 0.f };
        /** The button that drags a handle is down. */
        bool down = false;
        /** ...and went down this frame: what grabs a handle. */
        bool pressed = false;
    };

    /** @brief How a gizmo looks and snaps. @see DebugDraw::Gizmo */
    struct GizmoOptions {
        GizmoSpace space = GizmoSpace::eWorld;
        /** Its length on screen, in pixels, however near or far it is. */
        float size = 100.f;
        /** Steps it moves in: world units when translating, degrees when rotating, and a factor's step when scaling. 0 is none. */
        float snap = 0.f;
    };

    /**
     * @brief Shapes a scene draws to see what it is doing — lines, boxes, spheres, capsules, cameras and
     *        lights, outlined or filled — and gizmos for moving, turning and scaling things. Every scene
     *        has one, reached inside it as `Debug::` (Scene::Debug).
     *
     * Drawn from anywhere in the scene's frame — Update, FixedUpdate, a coroutine — and shown for that
     * frame, or for as long as a Style's duration says. Shown where a DebugDrawPass is added to a
     * graph (with that graph's camera; a view with its own), or wherever Render is called.
     *
     * Fills are drawn after what is opaque, and the ones you can see through back to front, so a
     * see-through box shows what is inside it — sorted for the first camera to draw them in a frame.
     *
     * @code
     * void MyScene::Update() {
     *     Debug::Box(body.bounds.min, body.bounds.max, {.color = {0, 1, 0, 1}, .fill = {0, 1, 0, 0.15f}});
     *     Debug::Arrow(ray.origin, ray.origin + ray.direction * 10.f, {.color = {1, 1, 0, 1}, .duration = 2.f});
     *     Debug::SpotLight(lamp.position, lamp.direction, lamp.range, lamp.outerAngle);
     *     if (Debug::Gizmo(kor::GizmoMode::eTranslate, selected.transform, camera.ViewProjection()))
     *         selected.dirty = true;
     * }
     * void MyScene::Initialize() {
     *     Graph().Add<kor::DebugDrawPass>(SceneDebug(), [this] { return camera.ViewProjection(); });
     * }
     * @endcode
     */
    class KORAL_API DebugDraw {
    public:
        /** @brief How a shape is drawn. @see DebugStyle */
        using Style = DebugStyle;

        DebugDraw();
        ~DebugDraw();
        DebugDraw(const DebugDraw&) = delete;
        DebugDraw& operator=(const DebugDraw&) = delete;

        // ---- lines --------------------------------------------------------------------------------

        void Line(kor::Vec3 from, kor::Vec3 to, const Style& style = {});
        /** @brief A line with a head at @p to; filled, the head is a cone. */
        void Arrow(kor::Vec3 from, kor::Vec3 to, const Style& style = {});
        /** @brief A small cross: a point you can see. */
        void Point(kor::Vec3 position, float size = 0.1f, const Style& style = {});
        /** @brief @p transform's X, Y and Z axes, in red, green and blue. */
        void Axes(const kor::Mat4& transform, float size = 1.f, float duration = 0.f);
        /** @brief A grid of @p cells by @p cells on the XZ plane. */
        void Grid(kor::Vec3 center, float size, int cells, const Style& style = {});
        /** @brief What a camera with @p viewProjection sees: its frustum's twelve edges. */
        void Frustum(const kor::Mat4& viewProjection, const Style& style = {});

        // ---- shapes, outlined or filled -----------------------------------------------------------

        void Triangle(kor::Vec3 a, kor::Vec3 b, kor::Vec3 c, const Style& style = {});
        /** @brief Four corners, in order around the edge. */
        void Quad(kor::Vec3 a, kor::Vec3 b, kor::Vec3 c, kor::Vec3 d, const Style& style = {});
        /** @brief A rectangle of @p size facing along @p normal. */
        void Plane(kor::Vec3 center, kor::Vec3 normal, kor::Vec2 size, const Style& style = {});
        /** @brief Filled, a disc. */
        void Circle(kor::Vec3 center, kor::Vec3 normal, float radius, const Style& style = {}, int segments = 32);
        /** @brief An axis-aligned box. */
        void Box(kor::Vec3 min, kor::Vec3 max, const Style& style = {});
        /** @brief The unit cube (-0.5 to 0.5) moved by @p transform: a box in any orientation. */
        void Box(const kor::Mat4& transform, const Style& style = {});
        /** @brief Outlined, three circles, one per axis; filled, the sphere. */
        void Sphere(kor::Vec3 center, float radius, const Style& style = {}, int segments = 32);
        void Cylinder(kor::Vec3 from, kor::Vec3 to, float radius, const Style& style = {}, int segments = 24);
        /** @brief A cone with its base's centre at @p base and its point at @p tip. */
        void Cone(kor::Vec3 base, kor::Vec3 tip, float radius, const Style& style = {}, int segments = 24);
        /** @brief A cylinder from @p from to @p to with a half-sphere at each end. */
        void Capsule(kor::Vec3 from, kor::Vec3 to, float radius, const Style& style = {}, int segments = 24);

        // ---- cameras and lights -------------------------------------------------------------------

        /**
         * @brief A camera: the pyramid it sees through, @p size deep, with a triangle on top for which way
         *        is up. Any projection — perspective or orthographic — and any convention it was made with.
         */
        void Camera(const kor::Mat4& view, const kor::Mat4& projection, float size = 1.f, const Style& style = {});
        /** @brief A point light: a star where it is, and the sphere it reaches to (none when @p range is 0: no limit). */
        void PointLight(kor::Vec3 position, float range, const Style& style = {});
        /**
         * @brief A spot light: the cone it lights, out to @p range (1 when 0: no limit). Angles are from the
         *        centre to the edge, in radians; @p innerAngle (the fully lit cone) is a second ring when not 0.
         */
        void SpotLight(kor::Vec3 position, kor::Vec3 direction, float range, float outerAngle, float innerAngle = 0.f,
                       const Style& style = {});
        /** @brief A directional light — the sun: a disc at @p position with its rays, @p size long. */
        void DirectionalLight(kor::Vec3 position, kor::Vec3 direction, float size = 1.f, const Style& style = {});

        // ---- gizmos -------------------------------------------------------------------------------

        /**
         * @brief A gizmo on @p transform: its handles drawn this frame, and the one under @p pointer dragged
         *        while its button is down. Call it every frame the thing is selected — it is what keeps the
         *        drag going.
         *
         * Translating has an arrow per axis, a square per plane and a ring in the middle to move along the
         * screen; rotating, a ring per axis and one around the screen; scaling, a handle per axis and a
         * cube in the middle for all three at once. Handles seen end-on are hidden: they would only jump.
         *
         * @param id Tells gizmos apart across frames; 0 is "the n-th gizmo drawn this frame", which is
         *           enough while the same ones are drawn in the same order.
         * @return Whether @p transform changed.
         */
        bool Gizmo(GizmoMode mode, kor::Mat4& transform, const kor::Mat4& viewProjection, const GizmoPointer& pointer,
                   const GizmoOptions& options = {}, std::uint64_t id = 0);
        /** @brief A handle is being dragged: the pointer is the gizmo's, not the camera's or the picking's. */
        [[nodiscard]] bool GizmoActive() const { return _drag.has_value(); }
        /** @brief The pointer is over a handle, this frame or the last: a click now would grab it. */
        [[nodiscard]] bool GizmoHovered() const { return _hovered || _hoveredBefore; }

        // ---- the frame ----------------------------------------------------------------------------

        /** @brief Everything being drawn, now. */
        void Clear();
        /**
         * @brief For a DebugDraw that is not a scene's own — a scene's is moved on by the application: the
         *        last frame is over, its lines go (those with time left keep it, less @p frameTime), and
         *        what is drawn from now on is the next frame's. Call it once a frame, before drawing.
         */
        void NextFrame(float frameTime);
        [[nodiscard]] std::size_t LineCount() const { return _lines.size(); }
        [[nodiscard]] std::size_t TriangleCount() const { return _solidVertices.size() / 3; }

        /**
         * @brief Draws the shapes into @p target, over what it holds: depth-tested against its depth
         *        attachment when it has one, over everything when not. On the thread the scene runs on.
         */
        void Render(CommandBuffer& commandBuffer, const kor::Mat4& viewProjection, ResourceRef<const Framebuffer> target);

        /**
         * @brief For a pass: brings the GPU's copy of the shapes, and the pipelines for @p target, up to
         *        date — the fills you can see through sorted for @p viewProjection. Main thread.
         */
        void Prepare(const ResourceRef<const Framebuffer>& target, const kor::Mat4& viewProjection);
        /** @brief For a pass: the draws, after Prepare. Any thread. */
        void Record(CommandBuffer& commandBuffer, const kor::Mat4& viewProjection, const ResourceRef<const Framebuffer>& target) const;

    private:
        friend class App;
        /** @brief A new frame: what the GPU holds is stale. */
        void BeginFrame(std::uint64_t frame);
        /** @brief The frame is over: what was for it goes, what has time left keeps it. */
        void EndFrame(float frameTime);

        struct Stored {
            kor::Vec3 from, to;
            kor::Vec4 color;
            float remaining;
            bool onTop;
            float width;
        };
        /** Triangles of one shape, at [first, first + count) of _solidVertices. */
        struct Solid {
            std::uint32_t first, count;
            kor::Vec4 color;
            kor::Vec3 center;
            float remaining;
            bool onTop;
        };
        struct Vertex {
            kor::Vec4 position;
            kor::Vec4 color;
        };

        /**
         * What is drawn, in the order it is drawn, each a range of the buffer and a pipeline. Wide lines
         * are two vertices a line in the buffer, like the others, and six a line drawn: a quad.
         */
        enum Batch : std::uint8_t {
            eOpaqueFills, eLines, eWideLines, eSeeThroughFills, eFillsOnTop, eLinesOnTop, eWideLinesOnTop, eBatchCount
        };
        struct Target {
            std::array<Resource<GraphicsPipeline>, eBatchCount> pipelines;
            std::array<Resource<DescriptorSet>, eBatchCount> sets;
            const void* buffer = nullptr;   ///< The buffer the sets were written for.
        };

        /** A line of a shape's outline: none when the style says no outline. */
        void Edge(kor::Vec3 from, kor::Vec3 to, const Style& style);
        /** A circle's outline alone. */
        void Ring(kor::Vec3 center, kor::Vec3 normal, float radius, const Style& style, int segments);
        /** Starts a filled shape, when @p style has a fill: its triangles are the ones added until EndFill. */
        bool BeginFill(const Style& style);
        void FillTriangle(kor::Vec3 a, kor::Vec3 b, kor::Vec3 c);
        void EndFill();

        struct GizmoDrag {
            std::uint64_t key;
            GizmoMode mode;
            int handle;
            kor::Mat4 start;
            kor::Vec3 axis;         ///< The axis it moves along, or turns around; the plane's normal for a plane.
            kor::Vec3 grab;         ///< Where on the axis or plane it was grabbed; for a ring, the direction to the grab.
            float grabParam;        ///< Where along the axis it was grabbed.
            kor::Vec2 grabCursor;
            float angle;            ///< A ring's turn so far, and the cursor's last angle around the centre.
            float lastCursorAngle;
            float screenSign;       ///< Which way a turn on screen turns around the axis.
            bool seen;
        };

        std::vector<Stored> _lines;
        std::vector<kor::Vec3> _solidVertices;
        std::vector<Solid> _solids;
        std::optional<Solid> _filling;
        Resource<Buffer> _buffer;
        std::size_t _capacity = 0;          // in vertices
        std::array<std::uint32_t, eBatchCount> _counts {};
        std::uint64_t _frame = 0, _uploaded = ~std::uint64_t(0);
        bool _changed = true;   ///< Shapes added or gone since the GPU's copy was made.
        std::map<std::string, Target> _targets;   // by the target's formats

        std::optional<GizmoDrag> _drag;
        std::uint64_t _gizmoCalls = 0;
        bool _hovered = false, _hoveredBefore = false;
    };

    /**
     * @brief A render pass drawing a scene's debug shapes into @p target — the screen by default —
     *        with the camera @p viewProjection returns, read on the main thread each frame.
     * @param depth A depth image of the graph's to test the shapes against; empty draws them on top.
     *              Solid fills write to it, so what is drawn after the pass is hidden behind them.
     */
    class KORAL_API DebugDrawPass final : public RenderPass {
    public:
        DebugDrawPass(DebugDraw& draw, std::function<kor::Mat4()> viewProjection,
                      std::string target = std::string(FrameGraph::Screen), std::string depth = {});

        void Setup(PassBuilder& builder) override;
        void Initialize(const PassResources& resources) override;
        void Prepare() override;
        void Record(CommandBuffer& commandBuffer) const override;

    private:
        DebugDraw& _draw;
        std::function<kor::Mat4()> _viewProjection;
        std::string _target, _depth;
        Resource<Framebuffer> _framebuffer;
        kor::Mat4 _matrix { 1.f };
    };
}
