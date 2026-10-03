//
// koral-ui: drawing a layer tree into a frame.
//

#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include <commandBuffer.h>
#include <frameGraph.h>
#include <framebuffer.h>
#include <resource.h>

#include "api.h"
#include "canvas.h"

namespace kui
{
    /**
     * @brief Draws a tree of layers: what turns pictures into GPU work, once a frame.
     *
     * The frame is a few storage buffers every UI draw shares. When nothing in the tree changed since
     * the last frame, preparing it does nothing and uploads nothing; when a layer only moved (a scroll,
     * an animation), only the layer table is rewritten; when a picture changed, the frame is put
     * together again and only the bytes that differ are uploaded.
     *
     * Usually reached through a UiPass. @see UiPass
     */
    class KUI_API Renderer {
    public:
        Renderer();
        ~Renderer();
        Renderer(const Renderer&) = delete;
        Renderer& operator=(const Renderer&) = delete;

        void SetRoot(std::shared_ptr<Layer> root);
        [[nodiscard]] const std::shared_ptr<Layer>& Root() const;
        /** @brief Pixels per logical unit: the display's scale factor. */
        void SetScale(float scale);
        [[nodiscard]] float Scale() const;

        /** @brief Brings the GPU's copy of the frame up to date for @p target. Main thread. */
        void Prepare(const kor::ResourceRef<const kor::Framebuffer>& target);
        /** @brief The draws, over what @p target holds. After Prepare; any thread. */
        void Record(kor::CommandBuffer& commandBuffer, const kor::ResourceRef<const kor::Framebuffer>& target) const;

        struct Statistics {
            std::size_t instances = 0, vertices = 0, layers = 0, clips = 0, draws = 0;
            std::size_t uploadedBytes = 0;     ///< Last Prepare.
            double composeMs = 0.0;            ///< Last Prepare's CPU time putting the frame together.
            bool recomposed = false;           ///< Whether the last Prepare had to put the frame together again.
        };
        [[nodiscard]] const Statistics& Stats() const;

        struct Impl;
    private:
        std::unique_ptr<Impl> _impl;
    };

    class Ui;

    /**
     * @brief A frame-graph pass drawing a Ui (or a bare renderer's layers) over @p target — the screen
     *        by default.
     *
     * @code
     * auto root = kui::Layer::Create();
     * root->SetPicture(canvas.Finish());
     * _ui.SetRoot(root);                          // kui::Renderer _ui; a member of the scene
     * Graph().Add<kui::UiPass>(_ui);               // or a kui::Ui, for widgets
     * @endcode
     */
    class KUI_API UiPass final : public kor::RenderPass {
    public:
        explicit UiPass(Renderer& renderer, std::string target = std::string(kor::FrameGraph::Screen));
        /** @brief Draws a widget tree: @p ui's renderer. */
        explicit UiPass(Ui& ui, std::string target = std::string(kor::FrameGraph::Screen));

        void Setup(kor::PassBuilder& builder) override;
        void Initialize(const kor::PassResources& resources) override;
        void Prepare() override;
        void Record(kor::CommandBuffer& commandBuffer) const override;

    private:
        Renderer& _renderer;
        std::string _target;
        kor::Resource<kor::Framebuffer> _framebuffer;
    };
}
