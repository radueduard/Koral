//
// Created by radue on 9/24/2026.
//

#pragma once

#include <concepts>
#include <cstdint>
#include <map>
#include <memory>
#include <atomic>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include <kmath/matrix.h>

#include "api.h"
#include "buffer.h"
#include "flags.h"
#include "image.h"
#include "reflect.h"
#include "resource.h"

namespace kor {
    class CommandBuffer;
    class FrameGraph;
    class Scene;
    class Framebuffer;
    class RenderPass;

    /**
     * @brief An image the frame graph creates for its passes.
     *
     * One image, not one per frame in flight: only the GPU touches it, and the GPU runs frames in
     * order. And not necessarily an image of its own: two the graph makes that are never needed at
     * the same time — same format, size and mips — share one (FrameGraph::SetAliasing). So what one
     * holds is only defined between the pass that writes it and the last one that uses it, this
     * frame; ReadPrevious is how a value lives on to the next.
     */
    struct ImageDesc {
        Image::Format format = Image::Format::eRGBA8_UNORM;
        /**
         * How the creating pass uses it. Every other pass adds its own through the usage it gives
         * Read, Write or Consume, and the image is made with all of them — so a pass that starts
         * sampling an image says so itself, rather than in whichever pass happens to create it.
         */
        Flags<Image::Usage> usage {};
        /** Size relative to the window, used when extent is unset: 0.5 is half resolution. */
        float scale = 1.f;
        /** What `scale` is relative to, when not the graph's screen: an image imported into the graph. */
        std::string sizeOf {};
        /** A fixed size instead, for targets that do not follow the window (a shadow map). */
        std::optional<kor::UVec2> extent {};
        kor::u32 mipLevels = 1;
    };

    /**
     * @brief A buffer the frame graph creates for its passes.
     *
     * A device-local one is shared with others never needed at the same time, as images are. One
     * the CPU writes (any other type — a CpuPass filling it, say) is one per frame in flight and
     * never shared, so a frame being written cannot disturb the one the GPU is still reading.
     */
    struct BufferDesc {
        kor::i64 size = 0;  ///< In bytes.
        /** How the creating pass uses it; other passes add theirs as for ImageDesc::usage. */
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
        /**
         * @brief Reads it, as @p usage — which the graph makes the image with, if it makes it.
         *
         * @code
         * .Read("depth", kor::Image::Usage::eSampled)
         * .Write("hdr", kor::Image::Usage::eStorage)
         * .Read("draws", kor::Buffer::Usage::eIndirect)
         * @endcode
         */
        PassBuilder& Read(std::string_view name, Flags<Image::Usage> usage);
        PassBuilder& Read(std::string_view name, Flags<Buffer::Usage> usage);
        /** @brief Modifies it in place; writers run in the order their passes were added. */
        PassBuilder& Write(std::string_view name);
        /** @brief Modifies it in place, as @p usage. @see Read(std::string_view, Flags<Image::Usage>) */
        PassBuilder& Write(std::string_view name, Flags<Image::Usage> usage);
        PassBuilder& Write(std::string_view name, Flags<Buffer::Usage> usage);
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
        /** @brief Consumes it, as @p usage. @see Read(std::string_view, Flags<Image::Usage>) */
        PassBuilder& Consume(std::string_view name, std::string_view as, Flags<Image::Usage> usage);
        PassBuilder& Consume(std::string_view name, std::string_view as, Flags<Buffer::Usage> usage);
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
         * Only for resources the graph creates. The copy is made with @p usage as well as the
         * resource's own.
         */
        PassBuilder& ReadPrevious(std::string_view name);
        PassBuilder& ReadPrevious(std::string_view name, Flags<Image::Usage> usage);
        PassBuilder& ReadPrevious(std::string_view name, Flags<Buffer::Usage> usage);

        /**
         * @brief Runs the pass on the async compute queue, alongside the graphics passes it does not
         *        depend on — SSAO beside the shadow maps, light culling beside the depth pre-pass.
         *
         * For a pass that records compute dispatches and copies only. The graph orders it against
         * the other queue by what it reads and writes, and keeps it apart from passes it is not
         * ordered against that need a resource it uses in another state: declare reads with their
         * usage (`Read("depth", Image::Usage::eSampled)`) so two passes that only sample the same
         * image may overlap. Resources it uses never share memory with others.
         *
         * A pass that uses the screen stays on the graphics queue. On a device with no second queue
         * (Context::SupportsAsyncCompute) it runs in order with the rest, which is always correct.
         */
        PassBuilder& AsyncCompute();

    private:
        friend class FrameGraph;
        struct Impl;
        explicit PassBuilder(Impl& impl) : _impl(impl) {}
        Impl& _impl;
    };

    /**
     * @brief A named group of resources the graph does not own, whose members change while it runs: the
     *        images and buffers each of a pipeline's entities holds.
     *
     * Imported under one name (FrameGraph::ImportSet), it is read and written by that name like any other
     * resource, and orders passes the same way: a pass that writes it runs before one that reads it. A pass
     * finds its members when it records (PassResources::SetNamed), so adding and removing them rebuilds
     * nothing — not the order, not any pass's Initialize. The barriers each member needs come from the
     * commands that use it, as for every resource.
     *
     * Changed between frames, from the thread the graph runs on; a change while passes record is refused.
     * With async compute on a queue family of its own, a pass using a set runs on the graphics queue: what
     * joins the set later need not have been made for both families.
     *
     * @code
     * auto entityImages = std::make_shared<kor::ResourceSet>();
     * graph.ImportSet("entity images", entityImages);
     * // each frame, as entities come and go:
     * entityImages->Add(image);
     * // in a pass: b.Write("entity images") in Setup, and in Record:
     * for (const auto& image : resources.SetNamed("entity images")->Images()) cb.ClearColorImage(image, color);
     * @endcode
     */
    class KORAL_API ResourceSet {
    public:
        void Add(ResourceRef<const Image> image);
        void Add(ResourceRef<const Buffer> buffer);
        /** @brief Takes out every member that is @p image (or @p buffer). */
        void Remove(const Image* image);
        void Remove(const Buffer* buffer);
        void Clear();

        [[nodiscard]] const std::vector<ResourceRef<const Image>>& Images() const { return _images; }
        [[nodiscard]] const std::vector<ResourceRef<const Buffer>>& Buffers() const { return _buffers; }
        [[nodiscard]] std::size_t Size() const { return _images.size() + _buffers.size(); }
        /** @brief Changes whenever the members do: for a pass that caches what it made of them. */
        [[nodiscard]] kor::u64 Generation() const { return _generation; }

    private:
        friend class FrameGraph;
        [[nodiscard]] bool Mutable(std::string_view what) const;
        std::vector<ResourceRef<const Image>> _images;
        std::vector<ResourceRef<const Buffer>> _buffers;
        kor::u64 _generation = 0;
        std::atomic<int> _recording = 0;   ///< Graphs recording passes with it now.
    };

    /** @brief Looks the graph's resources up by name, from RenderPass::Initialize on. */
    class KORAL_API PassResources {
    public:
        [[nodiscard]] ResourceRef<const Image> ImageNamed(std::string_view name) const;
        [[nodiscard]] ResourceRef<const Buffer> BufferNamed(std::string_view name) const;
        /**
         * @brief A buffer the graph made, to write from the CPU — what a CpuPass fills. Only for one
         *        made with a host-visible type; an imported buffer is its owner's to write.
         */
        [[nodiscard]] ResourceRef<Buffer> WritableBufferNamed(std::string_view name) const;
        /** @brief The size an image the graph made has (or will have, after a resize). */
        [[nodiscard]] kor::UVec2 Extent(std::string_view name) const;
        /** @brief Where last frame's @p name is kept, for a pass that declared PassBuilder::ReadPrevious. */
        [[nodiscard]] ResourceRef<const Image> PreviousImageNamed(std::string_view name) const;
        [[nodiscard]] ResourceRef<const Buffer> PreviousBufferNamed(std::string_view name) const;
        /** @brief The set imported as @p name (FrameGraph::ImportSet), or null. Its members are read as the pass records. */
        [[nodiscard]] const ResourceSet* SetNamed(std::string_view name) const;

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
     * - **Record** (any thread, every frame, alongside the other passes): record commands. It is
     *   `const`, so it cannot change the pass — per-frame values belong in Prepare — and the graph
     *   refuses to be changed while passes record. What it must not do on top of that is change
     *   state it shares with others, or wait for the main thread, which is blocked until every pass
     *   has recorded.
     *
     * Initialize runs again only when something the pass uses changed: a resource it names was
     * reallocated (the window resized, say) or an import it names now refers to something else.
     * A pass whose Initialize also depends on something outside the graph asks for it to run
     * again with RequestInitialize().
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
     *     void Record(kor::CommandBuffer& cb) const override { cb.BindComputePipeline(...).Dispatch(...); }
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
        virtual void Record(CommandBuffer& commandBuffer) const = 0;
        /**
         * @brief What can be changed about the pass while it runs, for an editor to show: an object of a
         *        reflected type (reflect.h), or nothing. kgui::PassSettings draws one for every pass.
         *
         * @code
         * struct BloomSettings { float threshold = 1.f; float intensity = 0.04f; };
         * KORAL_REFLECT(BloomSettings, threshold, intensity)
         * kor::Ref Settings() override { return _settings; }
         * @endcode
         */
        [[nodiscard]] virtual Ref Settings() { return {}; }
        /** @brief An editor changed Settings(): for what follows from them (a resource remade at another size). */
        virtual void SettingsChanged() {}

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

        /**
         * @brief Runs Initialize again before the next frame, although nothing the graph gives the
         *        pass has changed — for a pass whose Initialize also reads something else.
         */
        void RequestInitialize();

    private:
        friend class FrameGraph;
        friend class CpuPass;
        /** @brief Whether this runs on the CPU instead of recording GPU work. */
        [[nodiscard]] virtual bool RunsOnCpu() const { return false; }
        std::string _name;
        bool _enabled = true;
        bool _initializeRequested = false;
        FrameGraph* _graph = nullptr;
    };

    /**
     * @brief CPU work the frame graph schedules with the GPU work that uses it: filling a buffer the
     *        GPU reads, sorting a draw list, simulating something the frame then draws.
     *
     * Declares what it touches in Setup like any pass, and does its work in Run — on the
     * background pool, alongside the other passes, once the CPU passes it depends on have run.
     * GPU passes that depend on it record after it has finished, so their Record may use what it
     * computed. It runs before the GPU has done anything this frame, so it can depend only on
     * other CPU passes and on imports.
     *
     * What it writes for the GPU goes in a buffer the CPU can write: a BufferDesc with a
     * host-visible type (Buffer::Type::eDynamic), which the graph keeps one of per frame in flight.
     *
     * @code
     * class ParticleSim final : public kor::CpuPass {
     * public:
     *     ParticleSim() : CpuPass("Particles") {}
     *     void Setup(kor::PassBuilder& b) override {
     *         b.Create("particles", {.size = sizeof(Particle) * count, .usage = kor::Buffer::Usage::eStorage,
     *                                .type = kor::Buffer::Type::eDynamic});
     *     }
     *     void Initialize(const kor::PassResources& r) override { particles = r.BufferNamed("particles"); }
     *     void Run() override { step(); particles->Write(state); }
     * };
     * @endcode
     */
    class KORAL_API CpuPass : public RenderPass {
    public:
        using RenderPass::RenderPass;

        /** @brief The work: any thread, every frame. Not const — it is the pass's to change. */
        virtual void Run() = 0;

    private:
        void Record(CommandBuffer&) const final {}
        [[nodiscard]] bool RunsOnCpu() const final { return true; }
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
        /**
         * The main window's image. Write it to present; a pass that does is never dropped. Another
         * window's is its Window::ScreenName(), which a pass writes the same way.
         */
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
        /**
         * @brief Makes a group of resources whose members change at run time available to passes under one
         *        name. @see ResourceSet
         */
        void ImportSet(std::string name, std::shared_ptr<ResourceSet> set);

        /**
         * @brief Works the graph out again before the next frame: Setup, allocation, and Initialize
         *        for every pass whose resources changed.
         */
        void Invalidate();

        [[nodiscard]] bool empty() const { return _passes.empty(); }

        /**
         * @brief Runs one frame of the graph. The runtime calls this, inside the frame.
         * @return Whether any pass wrote the screen — so the runtime does not clear over it.
         */
        bool Execute();

        /** @brief Every pass added, in the order it was added — enabled or not, scheduled or not. */
        [[nodiscard]] std::vector<RenderPass*> Passes() const;

        /** @brief One pass as scheduled: its name, and how deep in the dependencies it sits. */
        struct Scheduled {
            std::string name;
            kor::u32 level;
            bool async = false;  ///< Runs on the async compute queue.
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
        /** @brief The resources kept for the next frame (RenderPass::ReadPrevious). */
        [[nodiscard]] const std::vector<std::string>& KeptForNextFrame() const { return _history; }
        /** @brief Whether the graph failed to compile, and so draws nothing (the log says why). */
        [[nodiscard]] bool Broken() const { return _broken; }

        /**
         * @brief What one pass costs, averaged over recent frames.
         *
         * GPU time is measured with a timer scope around everything the pass recorded — barriers
         * the frame put in front of its commands included — and arrives a few frames late, once
         * the GPU has run it. Zero, with gpuMeasured false, on a device that cannot timestamp.
         */
        struct PassTiming {
            std::string name;
            double gpuMs = 0.0;
            double recordMs = 0.0;    ///< CPU time recording it (on a background thread under Vulkan).
            double prepareMs = 0.0;   ///< CPU time in its Prepare, on the main thread.
            bool gpuMeasured = false;
        };
        /** @brief Every scheduled pass, in the order it runs. */
        [[nodiscard]] std::vector<PassTiming> PassTimings() const;

        /** @brief The graph's own share of a frame, averaged: what the main thread waits for, and what the GPU spends. */
        struct GraphTiming {
            double prepareMs = 0.0;     ///< Every Prepare, one after another.
            double recordWallMs = 0.0;  ///< From the first pass starting to record to the last finishing.
            double recordWorkMs = 0.0;  ///< Recording time summed over the passes; above recordWallMs when they overlap.
            double gpuMs = 0.0;         ///< The passes' GPU times, summed.
        };
        [[nodiscard]] GraphTiming Timing() const;
        /** @brief The last frames' CPU frame time and the graph's GPU time, in ms, oldest first. */
        struct TimeHistory {
            std::vector<float> frameMs;
            std::vector<float> gpuMs;
        };
        [[nodiscard]] TimeHistory Times() const;

        /**
         * @brief An image of the graph's by name — for a screenshot, a debug view, another scene.
         *        Empty before the first frame.
         *
         * An image asked for here is kept to itself from then on (not shared with others, see
         * SetAliasing), so what it holds after the frame is what the passes left in it. @p usage is
         * what the caller will do with it — Image::Usage::eTransferSrc to blit or copy it elsewhere —
         * and the graph makes the image with that from its next frame on (the image returned before
         * then may not have it yet: check UsageFlags()).
         */
        [[nodiscard]] ResourceRef<const Image> ImageNamed(std::string_view name, Flags<Image::Usage> usage = {}) const;

        /**
         * @brief Whether images (and device-local buffers) never needed at the same time share
         *        memory. On by default; off gives each its own, for inspecting them in a debugger.
         */
        void SetAliasing(bool enabled);
        [[nodiscard]] bool Aliasing() const { return _aliasing; }

        /** @brief What the graph's own resources take, with and without sharing. */
        struct MemoryUse {
            kor::u64 bytes = 0;          ///< Allocated: images, buffers and previous-frame copies.
            kor::u64 unsharedBytes = 0;  ///< What they would take with every resource on its own.
            kor::u32 resources = 0;      ///< Named resources the graph makes.
            kor::u32 allocations = 0;    ///< The images and buffers actually allocated for them.
        };
        [[nodiscard]] const MemoryUse& Memory() const { return _memory; }

        /** @brief Whether last frame's @p name holds a real previous frame. @see RenderPass::HasPrevious */
        [[nodiscard]] bool HasPrevious(std::string_view name) const;

    private:
        friend class PassResources;
        friend class RenderPass;
        friend class Scene;

        /** @brief The scene the graph belongs to; a graph that belongs to none uses the current scene. */
        [[nodiscard]] Scene* OwnerScene() const;
        Scene* _scene = nullptr;
        friend class View;
        Window* _target = nullptr;   ///< What Screen is: a View's target; the owner scene's window when null.
        [[nodiscard]] Window* TargetWindow() const;

        void Adopt(std::unique_ptr<RenderPass> pass);
        bool Build();
        /** @brief Whether the graph may be changed now: from its own thread, and not while passes record. */
        [[nodiscard]] bool Mutable(std::string_view what) const;

        // Shared with the timer callbacks, which arrive from the scheduler once a frame is done —
        // possibly after the graph is gone, hence held weakly by them.
        struct Stats;
        std::shared_ptr<Stats> _stats;


        std::vector<std::unique_ptr<RenderPass>> _passes;
        std::vector<RenderPass*> _order;      // what runs, in order
        std::vector<Scheduled> _schedule;
        std::vector<bool> _async;                                          // per position in _order
        std::vector<std::optional<std::size_t>> _waits;                    // per position: the other queue's pass it waits for
        std::vector<std::vector<ImageBarrier>> _handoffs;                  // per position: images it leaves for readers on both queues
        std::set<std::string> _demoted;                                    // async passes already told they run on graphics
        std::vector<std::string> _culled;
        std::vector<Skipped> _skipped;
        std::map<std::string, ResourceRef<const Image>, std::less<>> _images;    // every name, imported or made
        std::map<std::string, ResourceRef<const Buffer>, std::less<>> _buffers;
        std::map<std::string, ResourceRef<const Image>, std::less<>> _importedImages;
        std::map<std::string, ResourceRef<const Buffer>, std::less<>> _importedBuffers;
        std::map<std::string, std::shared_ptr<ResourceSet>, std::less<>> _importedSets;

        // ---- allocation -----------------------------------------------------------------------------
        // What was allocated, keyed by what it was allocated as, so a rebuild reuses whatever it still
        // fits — and only the passes whose resources actually changed are initialized again.
        struct Allocated {
            std::string key;
            kor::u64 id = 0;   // never reused, unlike an address
            Resource<Image> image;
            Resource<Buffer> buffer;
        };
        std::vector<Allocated> _allocated;
        std::map<std::string, kor::u64, std::less<>> _resourceIds;         // every name, aliases and imports included
        std::map<std::string, kor::u64, std::less<>> _importIds;           // bumped when an import changes
        std::map<const RenderPass*, std::vector<kor::u64>> _initializedWith; // what each pass was last initialized with
        std::vector<std::vector<std::size_t>> _dependencies;               // per position in _order
        std::vector<std::unique_ptr<RenderPass>> _refused;                 // added when the graph could not take them
        kor::u64 _nextId = 1;
        mutable std::map<std::string, Flags<Image::Usage>, std::less<>> _kept;   // asked for by name: never shared, and made with these too
        bool _aliasing = true;
        MemoryUse _memory;
        std::thread::id _owner;
        std::atomic<bool> _recording = false;

        // ---- previous frames ----------------------------------------------------------------------
        // Every resource some pass reads from the previous frame has a second copy that is not
        // per-frame: cleared when allocated, then refreshed from the resource after the frame's last
        // pass. Keyed by every name that reaches it, aliases included.
        std::map<std::string, ResourceRef<const Image>, std::less<>> _previousImages;
        std::map<std::string, ResourceRef<const Buffer>, std::less<>> _previousBuffers;
        struct History {
            std::string name;                // the physical resource
            std::string key;                 // what the copy was allocated as
            ResourceRef<const Image> image;  // this frame's
            ResourceRef<const Buffer> buffer;
            Resource<Image> previousImage;   // the copy kept
            Resource<Buffer> previousBuffer;
            kor::u64 id = 0;
            kor::u64 frames = 0;             // frames copied into it since it was made; 0 needs clearing
        };
        std::vector<History> _historyCopies;
        std::map<std::string, std::size_t, std::less<>> _historyIndex;     // every name reaching one, aliases included
        std::vector<Resource<Framebuffer>> _historyClears;  // what cleared a depth history, kept until rebuilt
        std::vector<std::string> _history;                  // the physical names, for KeptForNextFrame
        kor::UVec2 _extent {0, 0};
        /// The window's image the graph was last built for: which one, and how many times rebuilt.
        std::uintptr_t _screenSeen = 0;
        kor::u64 _screenGeneration = 0;
        mutable bool _dirty = true;
        bool _broken = false;
    };
}
