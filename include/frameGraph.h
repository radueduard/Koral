//
// Created by radue on 9/24/2026.
//

#pragma once

#include <concepts>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <glm/glm.hpp>

#include "api.h"
#include "buffer.h"
#include "flags.h"
#include "image.h"
#include "resource.h"

namespace kor {
    class CommandBuffer;
    class FrameGraph;
    class Framebuffer;
    class RenderPass;

    /** @brief An image the frame graph creates for its passes. */
    struct ImageDesc {
        Image::Format format = Image::Format::eRGBA8_UNORM;
        Flags<Image::Usage> usage = Image::Usage::eSampled;
        /** Size relative to the window, used when extent is unset: 0.5 is half resolution. */
        float scale = 1.f;
        /** A fixed size instead, for targets that do not follow the window (a shadow map). */
        std::optional<glm::uvec2> extent {};
        glm::u32 mipLevels = 1;
    };

    /** @brief A buffer the frame graph creates for its passes. */
    struct BufferDesc {
        glm::i64 size = 0;  ///< In bytes.
        Flags<Buffer::Usage> usage = Buffer::Usage::eStorage;
        Buffer::Type type = Buffer::Type::eDeviceLocal;
    };

    /**
     * @brief What a pass says about the resources it touches, from RenderPass::Setup.
     *
     * Resources are named. A name is either created by exactly one pass, or imported into the graph
     * from outside (FrameGraph::Import, and FrameGraph::Screen, which is always there). The graph
     * orders the passes from these declarations — creator first, then writers in the order they
     * were added, then readers — and drops passes whose results nothing needs.
     */
    class KORAL_API PassBuilder {
    public:
        /** @brief Reads what the passes before it left. */
        PassBuilder& Read(std::string_view name);
        /** @brief Modifies it in place; writers run in the order their passes were added. */
        PassBuilder& Write(std::string_view name);
        /** @brief Creates an image; the graph owns it and resizes it with the window. */
        PassBuilder& Create(std::string_view name, const ImageDesc& desc);
        /** @brief Creates a buffer; the graph owns it. */
        PassBuilder& Create(std::string_view name, const BufferDesc& desc);
        /**
         * @brief Modifies it in place and publishes the result under a new name.
         *
         * For a resource two groups of passes need in two states: those reading `name` see it as it
         * was and run first; those reading `as` see the result. One buffer, two versions — a GPU
         * cull that rewrites the draw list the depth pre-pass drew from, say.
         */
        PassBuilder& Consume(std::string_view name, std::string_view as);
        /** @brief Keeps the pass even when nothing reads what it makes (it has effects of its own). */
        PassBuilder& SideEffect();
        /**
         * @brief Reads it as it stood at the end of the previous frame.
         *
         * For temporal effects — TAA blending into last frame's result, reprojection, occlusion
         * culling against last frame's depth. It orders nothing: the pass may run before this frame
         * produces the resource, even when it is the pass that produces it. It does keep whatever
         * produces the resource running, since the next frame needs it.
         *
         * Reach it with PassResources::PreviousImageNamed (or PreviousBufferNamed): a separate image
         * the graph copies the resource into at the end of every frame, so a descriptor set built
         * once in Initialize stays right. Cleared (depth to 1, everything else to 0) whenever the
         * graph is rebuilt; RenderPass::HasPrevious says when it holds a real previous frame again.
         * Only for resources the graph creates.
         */
        PassBuilder& ReadPrevious(std::string_view name);

    private:
        friend class FrameGraph;
        struct Impl;
        explicit PassBuilder(Impl& impl) : _impl(impl) {}
        Impl& _impl;
    };

    /** @brief Looks the graph's resources up by name, from RenderPass::Initialize on. */
    class KORAL_API PassResources {
    public:
        [[nodiscard]] ResourceRef<const Image> ImageNamed(std::string_view name) const;
        [[nodiscard]] ResourceRef<const Buffer> BufferNamed(std::string_view name) const;
        /** @brief The size an image the graph made has (or will have, after a resize). */
        [[nodiscard]] glm::uvec2 Extent(std::string_view name) const;
        /** @brief Where last frame's @p name is kept, for a pass that declared PassBuilder::ReadPrevious. */
        [[nodiscard]] ResourceRef<const Image> PreviousImageNamed(std::string_view name) const;
        [[nodiscard]] ResourceRef<const Buffer> PreviousBufferNamed(std::string_view name) const;

    private:
        friend class FrameGraph;
        explicit PassResources(const FrameGraph& graph) : _graph(graph) {}
        const FrameGraph& _graph;
    };

    /**
     * @brief One step of a frame — a G-buffer, SSAO, a blur — that the frame graph schedules.
     *
     * Four hooks, each with its own rules about threads:
     * - **Setup** (main thread, when the graph is rebuilt): declare what it reads, writes and
     *   creates. Nothing else.
     * - **Initialize** (main thread, after allocation): build pipelines and descriptor sets from the
     *   graph's resources. Runs again whenever the graph is rebuilt — a resize, a pass toggled.
     * - **Prepare** (main thread, every frame): read the scene, write this frame's uniforms.
     * - **Record** (any thread, every frame, alongside the other passes): record commands. It must
     *   not change shared state or wait for the main thread, which is blocked until every pass has
     *   recorded.
     *
     * @code
     * class SSAOPass final : public kor::RenderPass {
     * public:
     *     SSAOPass() : RenderPass("SSAO") {}
     *     void Setup(kor::PassBuilder& b) override {
     *         b.Read("gbuffer.position").Read("gbuffer.normal")
     *          .Create("ssao", {.format = kor::Image::Format::eR8_UNORM,
     *                           .usage = kor::Image::Usage::eStorage | kor::Image::Usage::eSampled});
     *     }
     *     void Initialize(const kor::PassResources& r) override { ... build pipeline and set ... }
     *     void Record(kor::CommandBuffer& cb) override { cb.BindComputePipeline(...).Dispatch(...); }
     * };
     * @endcode
     */
    class KORAL_API RenderPass {
    public:
        explicit RenderPass(std::string name) : _name(std::move(name)) {}
        virtual ~RenderPass() = default;
        RenderPass(const RenderPass&) = delete;
        RenderPass& operator=(const RenderPass&) = delete;

        virtual void Setup(PassBuilder& builder) = 0;
        virtual void Initialize(const PassResources& resources) {}
        virtual void Prepare() {}
        virtual void Record(CommandBuffer& commandBuffer) = 0;
        /** @brief ImGui for the pass's own settings; FrameGraph::DrawGUI calls it. */
        virtual void DrawGUI() {}

        [[nodiscard]] const std::string& Name() const { return _name; }
        [[nodiscard]] bool Enabled() const { return _enabled; }
        /**
         * @brief Takes the pass out of the frame (or puts it back). Rebuilds the graph.
         *
         * What depended on it degrades rather than fails: a disabled pass that consumed a resource
         * hands it on unchanged, and passes that need something only a disabled pass creates are
         * skipped with it (FrameGraph::SkippedPasses).
         */
        void SetEnabled(bool enabled);

    protected:
        /**
         * @brief Whether last frame's @p name holds a real previous frame, rather than the cleared
         *        contents it starts with after the graph is (re)built.
         *
         * For a temporal effect to reset its accumulation after a resize. From Prepare and Record.
         */
        [[nodiscard]] bool HasPrevious(std::string_view name) const;

    private:
        friend class FrameGraph;
        std::string _name;
        bool _enabled = true;
        FrameGraph* _graph = nullptr;
    };

    /**
     * @brief Schedules render passes: orders them from what they declare, records them in parallel,
     *        and runs them ahead of the frame.
     *
     * Every Scene has one (Scene::Graph()); the runtime executes it each frame. Recording order does
     * not matter — barriers are worked out when the frame ends each pass's command buffer, which it
     * does in execution order — so every pass records at once, on the background pool.
     */
    class KORAL_API FrameGraph {
    public:
        /** The window's image. Write it to present; a pass that does is never dropped. */
        static constexpr std::string_view Screen = "screen";

        FrameGraph();
        ~FrameGraph();
        FrameGraph(const FrameGraph&) = delete;
        FrameGraph& operator=(const FrameGraph&) = delete;

        /** @brief Adds a pass. The order passes are added in breaks ties between passes free to run in either order. */
        template<std::derived_from<RenderPass> P, typename... Args>
        P& Add(Args&&... args) {
            auto pass = std::make_unique<P>(std::forward<Args>(args)...);
            P& ref = *pass;
            Adopt(std::move(pass));
            return ref;
        }

        /** @brief Makes a resource the graph does not own available to passes under a name. */
        void Import(std::string name, ResourceRef<const Image> image);
        void Import(std::string name, ResourceRef<const Buffer> buffer);

        /** @brief Rebuilds everything before the next frame: Setup, allocation, Initialize. */
        void Invalidate() { _dirty = true; }

        [[nodiscard]] bool empty() const { return _passes.empty(); }

        /**
         * @brief Runs one frame of the graph. The runtime calls this, inside the frame.
         * @return Whether any pass wrote the screen — so the runtime does not clear over it.
         */
        bool Execute();

        /** @brief Every pass's DrawGUI, plus a window showing the schedule. */
        void DrawGUI();

        /** @brief One pass as scheduled: its name, and how deep in the dependencies it sits. */
        struct Scheduled {
            std::string name;
            glm::u32 level;
        };
        /** @brief The passes that run, in order. Passes on one level do not depend on each other. */
        [[nodiscard]] const std::vector<Scheduled>& Schedule() const { return _schedule; }
        /** @brief A pass left out because something it needs is made by a disabled pass. */
        struct Skipped {
            std::string name;
            std::string resource;  ///< What it needed.
            std::string source;    ///< The disabled (or itself skipped) pass that makes it.
        };
        [[nodiscard]] const std::vector<Skipped>& SkippedPasses() const { return _skipped; }
        /** @brief Passes left out because nothing needs what they make. */
        [[nodiscard]] const std::vector<std::string>& CulledPasses() const { return _culled; }

        /** @brief An image of the graph's by name — for a screenshot or a debug view. Empty before the first frame. */
        [[nodiscard]] ResourceRef<const Image> ImageNamed(std::string_view name) const;

        /** @brief Whether last frame's @p name holds a real previous frame. @see RenderPass::HasPrevious */
        [[nodiscard]] bool HasPrevious(std::string_view name) const;

    private:
        friend class PassResources;
        struct Allocation;

        void Adopt(std::unique_ptr<RenderPass> pass);
        bool Build();

        std::vector<std::unique_ptr<RenderPass>> _passes;
        std::vector<RenderPass*> _order;      // what runs, in order
        std::vector<Scheduled> _schedule;
        std::vector<std::string> _culled;
        std::vector<Skipped> _skipped;
        std::map<std::string, ResourceRef<const Image>, std::less<>> _images;    // every name, imported or made
        std::map<std::string, ResourceRef<const Buffer>, std::less<>> _buffers;
        std::map<std::string, ResourceRef<const Image>, std::less<>> _importedImages;
        std::map<std::string, ResourceRef<const Buffer>, std::less<>> _importedBuffers;
        std::map<std::string, ImageDesc, std::less<>> _imageDescs;
        std::vector<Resource<Image>> _ownedImages;
        std::vector<Resource<Buffer>> _ownedBuffers;

        // ---- previous frames ----------------------------------------------------------------------
        // Every resource some pass reads from the previous frame has a second copy that is not
        // per-frame: cleared when allocated, then refreshed from the resource after the frame's last
        // pass. Keyed by every name that reaches it, aliases included.
        std::map<std::string, ResourceRef<const Image>, std::less<>> _previousImages;
        std::map<std::string, ResourceRef<const Buffer>, std::less<>> _previousBuffers;
        std::vector<std::pair<ResourceRef<const Image>, Resource<Image>>> _imageHistory;    // this frame's, kept copy
        std::vector<std::pair<ResourceRef<const Buffer>, Resource<Buffer>>> _bufferHistory;
        std::vector<Resource<Framebuffer>> _historyClears;  // what cleared a depth history, kept until rebuilt
        std::vector<std::string> _history;                  // the physical names, for the interface
        glm::u64 _historyFrames = 0;                        // frames copied since the history was (re)made
        glm::uvec2 _extent {0, 0};
        bool _dirty = true;
        bool _broken = false;
    };
}
