//
// Lines drawn for a frame, or a few seconds: what a scene draws to see what it is doing.
//

#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include <glm/glm.hpp>

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

    /** @brief How a debug shape is drawn. @see DebugDraw */
    struct DebugStyle {
        glm::vec4 color { 1.f, 1.f, 1.f, 1.f };
        /** Seconds of the scene's time to keep drawing it; 0 is this frame only. */
        float duration = 0.f;
        /** Drawn over everything, rather than hidden by what is in front of it. */
        bool onTop = false;
    };

    /**
     * @brief Lines a scene draws to see what it is doing: bounds, contacts, rays, paths, a camera's
     *        frustum. Every scene has one, reached inside it as `Debug::` (Scene::Debug).
     *
     * Drawn from anywhere in the scene's frame — Update, FixedUpdate, a coroutine — and shown for that
     * frame, or for as long as a Style's duration says. Shown where a DebugDrawPass is added to a
     * graph (with that graph's camera; a view with its own), or wherever Render is called.
     *
     * @code
     * void MyScene::Update() {
     *     Debug::Box(body.bounds.min, body.bounds.max, {.color = {0, 1, 0, 1}});
     *     Debug::Arrow(ray.origin, ray.origin + ray.direction * 10.f, {.color = {1, 1, 0, 1}, .duration = 2.f});
     *     Debug::Axes(transform);
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

        void Line(glm::vec3 from, glm::vec3 to, const Style& style = {});
        /** @brief An axis-aligned box. */
        void Box(glm::vec3 min, glm::vec3 max, const Style& style = {});
        /** @brief The unit cube (-0.5 to 0.5) moved by @p transform: a box in any orientation. */
        void Box(const glm::mat4& transform, const Style& style = {});
        void Circle(glm::vec3 center, glm::vec3 normal, float radius, const Style& style = {}, int segments = 32);
        /** @brief Three circles, one per axis. */
        void Sphere(glm::vec3 center, float radius, const Style& style = {}, int segments = 32);
        void Arrow(glm::vec3 from, glm::vec3 to, const Style& style = {});
        /** @brief A small cross: a point you can see. */
        void Point(glm::vec3 position, float size = 0.1f, const Style& style = {});
        /** @brief @p transform's X, Y and Z axes, in red, green and blue. */
        void Axes(const glm::mat4& transform, float size = 1.f, float duration = 0.f);
        /** @brief A grid of @p cells by @p cells on the XZ plane. */
        void Grid(glm::vec3 center, float size, int cells, const Style& style = {});
        /** @brief What a camera with @p viewProjection sees: its frustum's twelve edges. */
        void Frustum(const glm::mat4& viewProjection, const Style& style = {});

        /** @brief Everything being drawn, now. */
        void Clear();
        [[nodiscard]] std::size_t LineCount() const { return _lines.size(); }

        /**
         * @brief Draws the lines into @p target, over what it holds: depth-tested against its depth
         *        attachment when it has one, over everything when not. On the thread the scene runs on.
         */
        void Render(CommandBuffer& commandBuffer, const glm::mat4& viewProjection, ResourceRef<const Framebuffer> target);

        /** @brief For a pass: brings the GPU's copy of the lines, and a pipeline for @p target, up to date. Main thread. */
        void Prepare(const ResourceRef<const Framebuffer>& target);
        /** @brief For a pass: the draws, after Prepare. Any thread. */
        void Record(CommandBuffer& commandBuffer, const glm::mat4& viewProjection, const ResourceRef<const Framebuffer>& target) const;

    private:
        friend class App;
        /** @brief A new frame: what the GPU holds of the lines is stale. */
        void BeginFrame(std::uint64_t frame);
        /** @brief The frame is over: what was for it goes, what has time left keeps it. */
        void EndFrame(float frameTime);

        struct Stored {
            glm::vec3 from, to;
            glm::vec4 color;
            float remaining;
            bool onTop;
        };
        struct Vertex {
            glm::vec4 position;
            glm::vec4 color;
        };
        struct Target {
            Resource<GraphicsPipeline> tested, onTop;
            Resource<DescriptorSet> testedSet, onTopSet;
            const void* buffer = nullptr;   ///< The buffer the sets were written for.
        };

        std::vector<Stored> _lines;
        Resource<Buffer> _buffer;
        std::size_t _capacity = 0;          // in vertices
        std::uint32_t _testedCount = 0, _onTopCount = 0;
        std::uint64_t _frame = 0, _uploaded = ~std::uint64_t(0);
        std::map<std::string, Target> _targets;   // by the target's formats
    };

    /**
     * @brief A render pass drawing a scene's debug lines into @p target — the screen by default —
     *        with the camera @p viewProjection returns, read on the main thread each frame.
     * @param depth A depth image of the graph's to test the lines against; empty draws them on top.
     */
    class KORAL_API DebugDrawPass final : public RenderPass {
    public:
        DebugDrawPass(DebugDraw& draw, std::function<glm::mat4()> viewProjection,
                      std::string target = std::string(FrameGraph::Screen), std::string depth = {});

        void Setup(PassBuilder& builder) override;
        void Initialize(const PassResources& resources) override;
        void Prepare() override;
        void Record(CommandBuffer& commandBuffer) const override;

    private:
        DebugDraw& _draw;
        std::function<glm::mat4()> _viewProjection;
        std::string _target, _depth;
        Resource<Framebuffer> _framebuffer;
        glm::mat4 _matrix { 1.f };
    };
}
