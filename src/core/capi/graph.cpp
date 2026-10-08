//
// The C interface: the frame graph, and passes written as callbacks.
//

#include "capi.h"

#include <mutex>
#include <unordered_map>

#include "debugDraw.h"
#include "frameGraph.h"

using namespace kor;
using namespace kor::capi;

// ---- resource sets ------------------------------------------------------------------------------------------
// A KoralResourceSet is the ResourceSet itself. The ones made here are owned here, by a share each — which is
// what a graph that imports one shares — so the one a pass looks up and the one its maker holds are one pointer.

namespace
{
    std::mutex& SetsMutex() { static std::mutex m; return m; }
    std::unordered_map<ResourceSet*, std::shared_ptr<ResourceSet>>& OwnedSets() { static std::unordered_map<ResourceSet*, std::shared_ptr<ResourceSet>> sets; return sets; }

    ResourceSet& SetOf(KoralResourceSet* set)
    {
        if (!set) throw std::runtime_error("no resource set was given");
        return *reinterpret_cast<ResourceSet*>(set);
    }
}

namespace
{
    FrameGraph& GraphOf(KoralFrameGraph* graph)
    {
        if (!graph) throw std::runtime_error("no frame graph was given");
        return *reinterpret_cast<FrameGraph*>(graph);
    }

    PassBuilder& BuilderOf(KoralPassBuilder* builder) { return *reinterpret_cast<PassBuilder*>(builder); }

    /** The pass as the C side sees it; RenderPass and CpuPass differ, so both reach it through this. */
    class CPassBase {
    public:
        virtual ~CPassBase() = default;
        virtual RenderPass& Self() = 0;
        virtual bool CallHasPrevious(std::string_view name) const = 0;
        virtual void CallRequestInitialize() = 0;
    };

    KoralRenderPass* HandleOf(RenderPass& pass) { return reinterpret_cast<KoralRenderPass*>(&pass); }
    RenderPass& PassOf(KoralRenderPass* pass)
    {
        if (!pass) throw std::runtime_error("no pass was given");
        return *reinterpret_cast<RenderPass*>(pass);
    }

    // A RenderPass needs Record, a CpuPass Run: one of each, with the other hook absent.
    class CRenderPass final : public RenderPass, public CPassBase {
    public:
        CRenderPass(std::string name, const KoralPassCallbacks& callbacks) : RenderPass(std::move(name)), _c(callbacks) {}
        ~CRenderPass() override { if (_c.destroy) _c.destroy(_c.user); }
        RenderPass& Self() override { return *this; }
        bool CallHasPrevious(const std::string_view name) const override { return HasPrevious(name); }
        void CallRequestInitialize() override { RequestInitialize(); }
        void Setup(PassBuilder& builder) override {
            if (_c.setup) _c.setup(HandleOf(*this), reinterpret_cast<KoralPassBuilder*>(&builder), _c.user);
        }
        void Initialize(const PassResources& resources) override {
            if (_c.initialize) _c.initialize(HandleOf(*this), reinterpret_cast<KoralPassResources*>(const_cast<PassResources*>(&resources)), _c.user);
        }
        void Prepare() override { if (_c.prepare) _c.prepare(HandleOf(*this), _c.user); }
        void Record(CommandBuffer& commands) const override {
            if (_c.record) _c.record(HandleOf(const_cast<CRenderPass&>(*this)), reinterpret_cast<KoralCommandBuffer*>(&commands), _c.user);
        }
    private:
        KoralPassCallbacks _c;
    };

    class CCpuPass final : public CpuPass, public CPassBase {
    public:
        CCpuPass(std::string name, const KoralPassCallbacks& callbacks) : CpuPass(std::move(name)), _c(callbacks) {}
        ~CCpuPass() override { if (_c.destroy) _c.destroy(_c.user); }
        RenderPass& Self() override { return *this; }
        bool CallHasPrevious(const std::string_view name) const override { return HasPrevious(name); }
        void CallRequestInitialize() override { RequestInitialize(); }
        void Setup(PassBuilder& builder) override {
            if (_c.setup) _c.setup(HandleOf(*this), reinterpret_cast<KoralPassBuilder*>(&builder), _c.user);
        }
        void Initialize(const PassResources& resources) override {
            if (_c.initialize) _c.initialize(HandleOf(*this), reinterpret_cast<KoralPassResources*>(const_cast<PassResources*>(&resources)), _c.user);
        }
        void Prepare() override { if (_c.prepare) _c.prepare(HandleOf(*this), _c.user); }
        void Run() override { if (_c.run) _c.run(HandleOf(*this), _c.user); }
    private:
        KoralPassCallbacks _c;
    };

    CPassBase& CPassOf(KoralRenderPass* pass)
    {
        auto* c = dynamic_cast<CPassBase*>(&PassOf(pass));
        if (!c) throw std::runtime_error("only a pass written through this interface can ask that");
        return *c;
    }

    const PassResources& ResourcesOf(KoralPassResources* resources)
    {
        if (!resources) throw std::runtime_error("no pass resources were given");
        return *reinterpret_cast<const PassResources*>(resources);
    }

    // kind: 0 neither, 1 image, 2 buffer.
    template<typename Named, typename Image, typename Buffer>
    void ByKind(const uint32_t kind, Named&& named, Image&& image, Buffer&& buffer)
    {
        if (kind == 1) image();
        else if (kind == 2) buffer();
        else named();
    }

    thread_local std::string t_resource, t_source;
}

extern "C" {

KoralRenderPass* koral_graph_add(KoralFrameGraph* graph, const char* name, const KoralPassCallbacks* pass)
{
    if (!pass) { Fail("koral_graph_add needs callbacks"); return nullptr; }
    return Guarded([&]() -> KoralRenderPass* {
        auto& g = GraphOf(graph);
        const std::string passName = name ? name : "";
        if (pass->run) return HandleOf(g.Add<CCpuPass>(passName, *pass));
        return HandleOf(g.Add<CRenderPass>(passName, *pass));
    }, static_cast<KoralRenderPass*>(nullptr));
}
void koral_graph_import_image(KoralFrameGraph* g, const char* name, KoralImage* image)
{
    GuardedVoid([&] { GraphOf(g).Import(name ? name : "", RefOf<Image>(image)); });
}
void koral_graph_import_buffer(KoralFrameGraph* g, const char* name, KoralBuffer* buffer)
{
    GuardedVoid([&] { GraphOf(g).Import(name ? name : "", RefOf<Buffer>(buffer)); });
}
void koral_graph_import_set(KoralFrameGraph* g, const char* name, KoralResourceSet* set)
{
    GuardedVoid([&] {
        std::shared_ptr<ResourceSet> shared;
        {
            std::scoped_lock lock(SetsMutex());
            const auto it = OwnedSets().find(reinterpret_cast<ResourceSet*>(set));
            if (it == OwnedSets().end()) throw std::runtime_error("only a set made with koral_resource_set_new can be imported");
            shared = it->second;
        }
        GraphOf(g).ImportSet(name ? name : "", std::move(shared));
    });
}
void koral_graph_invalidate(KoralFrameGraph* g) { GuardedVoid([&] { GraphOf(g).Invalidate(); }); }
bool koral_graph_empty(KoralFrameGraph* g) { return Guarded([&] { return GraphOf(g).empty(); }, true); }
uint32_t koral_graph_schedule_count(KoralFrameGraph* g) { return Guarded([&] { return static_cast<uint32_t>(GraphOf(g).Schedule().size()); }, 0u); }
const char* koral_graph_schedule(KoralFrameGraph* g, const uint32_t i, uint32_t* level, bool* async)
{
    return Guarded([&] {
        const auto& entry = GraphOf(g).Schedule().at(i);
        if (level) *level = entry.level;
        if (async) *async = entry.async;
        return Keep(entry.name);
    }, Keep(""));
}
uint32_t koral_graph_culled_pass_count(KoralFrameGraph* g) { return Guarded([&] { return static_cast<uint32_t>(GraphOf(g).CulledPasses().size()); }, 0u); }
const char* koral_graph_culled_pass(KoralFrameGraph* g, const uint32_t i) { return Guarded([&] { return Keep(GraphOf(g).CulledPasses().at(i)); }, Keep("")); }
uint32_t koral_graph_skipped_pass_count(KoralFrameGraph* g) { return Guarded([&] { return static_cast<uint32_t>(GraphOf(g).SkippedPasses().size()); }, 0u); }
const char* koral_graph_skipped_pass(KoralFrameGraph* g, const uint32_t i, const char** resource, const char** source)
{
    return Guarded([&] {
        const auto& skipped = GraphOf(g).SkippedPasses().at(i);
        t_resource = skipped.resource;
        t_source = skipped.source;
        if (resource) *resource = t_resource.c_str();
        if (source) *source = t_source.c_str();
        return Keep(skipped.name);
    }, Keep(""));
}
KoralImage* koral_graph_image_named(KoralFrameGraph* g, const char* name, const uint32_t usage)
{
    return Guarded([&] { return Borrow(GraphOf(g).ImageNamed(name ? name : "", FlagsOf<Image::Usage>(usage))); },
                   static_cast<KoralResource*>(nullptr));
}
void koral_graph_set_aliasing(KoralFrameGraph* g, const bool enabled) { GuardedVoid([&] { GraphOf(g).SetAliasing(enabled); }); }
bool koral_graph_aliasing(KoralFrameGraph* g) { return Guarded([&] { return GraphOf(g).Aliasing(); }, false); }
void koral_graph_memory(KoralFrameGraph* g, uint64_t* bytes, uint64_t* unshared, uint32_t* resources, uint32_t* allocations)
{
    GuardedVoid([&] {
        const auto& m = GraphOf(g).Memory();
        if (bytes) *bytes = m.bytes;
        if (unshared) *unshared = m.unsharedBytes;
        if (resources) *resources = m.resources;
        if (allocations) *allocations = m.allocations;
    });
}
void koral_graph_timing(KoralFrameGraph* g, double* prepare, double* recordWall, double* recordWork, double* gpu)
{
    GuardedVoid([&] {
        const auto t = GraphOf(g).Timing();
        if (prepare) *prepare = t.prepareMs;
        if (recordWall) *recordWall = t.recordWallMs;
        if (recordWork) *recordWork = t.recordWorkMs;
        if (gpu) *gpu = t.gpuMs;
    });
}
bool koral_graph_has_previous(KoralFrameGraph* g, const char* name) { return Guarded([&] { return GraphOf(g).HasPrevious(name ? name : ""); }, false); }

const char* koral_pass_name(KoralRenderPass* p) { return Guarded([&] { return Keep(PassOf(p).Name()); }, Keep("")); }
bool koral_pass_enabled(KoralRenderPass* p) { return Guarded([&] { return PassOf(p).Enabled(); }, false); }
void koral_pass_set_enabled(KoralRenderPass* p, const bool enabled) { GuardedVoid([&] { PassOf(p).SetEnabled(enabled); }); }
bool koral_pass_has_previous(KoralRenderPass* p, const char* name) { return Guarded([&] { return CPassOf(p).CallHasPrevious(name ? name : ""); }, false); }
void koral_pass_request_initialize(KoralRenderPass* p) { GuardedVoid([&] { CPassOf(p).CallRequestInitialize(); }); }

void koral_pass_builder_read(KoralPassBuilder* b, const char* name, const uint32_t kind, const uint32_t usage)
{
    GuardedVoid([&] {
        auto& x = BuilderOf(b);
        const std::string_view n = name ? name : "";
        ByKind(kind, [&] { x.Read(n); }, [&] { x.Read(n, FlagsOf<Image::Usage>(usage)); }, [&] { x.Read(n, FlagsOf<Buffer::Usage>(usage)); });
    });
}
void koral_pass_builder_write(KoralPassBuilder* b, const char* name, const uint32_t kind, const uint32_t usage)
{
    GuardedVoid([&] {
        auto& x = BuilderOf(b);
        const std::string_view n = name ? name : "";
        ByKind(kind, [&] { x.Write(n); }, [&] { x.Write(n, FlagsOf<Image::Usage>(usage)); }, [&] { x.Write(n, FlagsOf<Buffer::Usage>(usage)); });
    });
}
void koral_pass_builder_read_previous(KoralPassBuilder* b, const char* name, const uint32_t kind, const uint32_t usage)
{
    GuardedVoid([&] {
        auto& x = BuilderOf(b);
        const std::string_view n = name ? name : "";
        ByKind(kind, [&] { x.ReadPrevious(n); }, [&] { x.ReadPrevious(n, FlagsOf<Image::Usage>(usage)); },
               [&] { x.ReadPrevious(n, FlagsOf<Buffer::Usage>(usage)); });
    });
}
void koral_pass_builder_consume(KoralPassBuilder* b, const char* name, const char* as, const uint32_t kind, const uint32_t usage)
{
    GuardedVoid([&] {
        auto& x = BuilderOf(b);
        const std::string_view n = name ? name : "", a = as ? as : "";
        ByKind(kind, [&] { x.Consume(n, a); }, [&] { x.Consume(n, a, FlagsOf<Image::Usage>(usage)); },
               [&] { x.Consume(n, a, FlagsOf<Buffer::Usage>(usage)); });
    });
}
void koral_pass_builder_create_image(KoralPassBuilder* b, const char* name, const KoralImageDesc* d)
{
    GuardedVoid([&] {
        ImageDesc desc{
            .format = static_cast<Image::Format>(d->format),
            .usage = FlagsOf<Image::Usage>(d->usage),
            .scale = d->scale,
            .sizeOf = d->size_of ? d->size_of : "",
            .mipLevels = d->mip_levels ? d->mip_levels : 1,
        };
        if (d->has_extent) desc.extent = kor::UVec2(d->extent[0], d->extent[1]);
        BuilderOf(b).Create(name ? name : "", desc);
    });
}
void koral_pass_builder_create_buffer(KoralPassBuilder* b, const char* name, const KoralBufferDesc* d)
{
    GuardedVoid([&] {
        BuilderOf(b).Create(name ? name : "", BufferDesc{.size = d->size, .usage = FlagsOf<Buffer::Usage>(d->usage),
                                                          .type = static_cast<Buffer::Type>(d->type)});
    });
}
void koral_pass_builder_side_effect(KoralPassBuilder* b) { GuardedVoid([&] { BuilderOf(b).SideEffect(); }); }
void koral_pass_builder_async_compute(KoralPassBuilder* b) { GuardedVoid([&] { BuilderOf(b).AsyncCompute(); }); }

KoralImage* koral_pass_resources_image_named(KoralPassResources* r, const char* name)
{
    return Guarded([&] { return Borrow(ResourcesOf(r).ImageNamed(name ? name : "")); }, static_cast<KoralResource*>(nullptr));
}
KoralBuffer* koral_pass_resources_buffer_named(KoralPassResources* r, const char* name)
{
    return Guarded([&] { return Borrow(ResourcesOf(r).BufferNamed(name ? name : "")); }, static_cast<KoralResource*>(nullptr));
}
KoralBuffer* koral_pass_resources_writable_buffer_named(KoralPassResources* r, const char* name)
{
    return Guarded([&] { return BorrowWritable(ResourcesOf(r).WritableBufferNamed(name ? name : "")); }, static_cast<KoralResource*>(nullptr));
}
void koral_pass_resources_extent(KoralPassResources* r, const char* name, uint32_t* x, uint32_t* y)
{
    const auto e = Guarded([&] { return ResourcesOf(r).Extent(name ? name : ""); }, kor::UVec2(0));
    if (x) *x = e.x;
    if (y) *y = e.y;
}
KoralImage* koral_pass_resources_previous_image_named(KoralPassResources* r, const char* name)
{
    return Guarded([&] { return Borrow(ResourcesOf(r).PreviousImageNamed(name ? name : "")); }, static_cast<KoralResource*>(nullptr));
}
KoralBuffer* koral_pass_resources_previous_buffer_named(KoralPassResources* r, const char* name)
{
    return Guarded([&] { return Borrow(ResourcesOf(r).PreviousBufferNamed(name ? name : "")); }, static_cast<KoralResource*>(nullptr));
}
KoralResourceSet* koral_pass_resources_set_named(KoralPassResources* r, const char* name)
{
    return Guarded([&] {
        return reinterpret_cast<KoralResourceSet*>(const_cast<ResourceSet*>(ResourcesOf(r).SetNamed(name ? name : "")));
    }, static_cast<KoralResourceSet*>(nullptr));
}

KoralResourceSet* koral_resource_set_new(void)
{
    return Guarded([] {
        auto set = std::make_shared<ResourceSet>();
        std::scoped_lock lock(SetsMutex());
        auto* raw = set.get();
        OwnedSets().emplace(raw, std::move(set));
        return reinterpret_cast<KoralResourceSet*>(raw);
    }, static_cast<KoralResourceSet*>(nullptr));
}
void koral_resource_set_destroy(KoralResourceSet* set)
{
    std::scoped_lock lock(SetsMutex());
    OwnedSets().erase(reinterpret_cast<ResourceSet*>(set));
}
void koral_resource_set_add(KoralResourceSet* set, KoralResource* resource)
{
    GuardedVoid([&] {
        if (!resource) throw std::runtime_error("no resource was given");
        if (resource->Kind() == KORAL_RESOURCE_IMAGE) SetOf(set).Add(RefOf<Image>(resource));
        else if (resource->Kind() == KORAL_RESOURCE_BUFFER) SetOf(set).Add(RefOf<Buffer>(resource));
        else throw std::runtime_error("a resource set holds images and buffers");
    });
}
void koral_resource_set_remove(KoralResourceSet* set, KoralResource* resource)
{
    GuardedVoid([&] {
        if (!resource) return;
        if (resource->Kind() == KORAL_RESOURCE_IMAGE) SetOf(set).Remove(RefOf<Image>(resource).Get());
        else if (resource->Kind() == KORAL_RESOURCE_BUFFER) SetOf(set).Remove(RefOf<Buffer>(resource).Get());
    });
}
void koral_resource_set_clear(KoralResourceSet* set) { GuardedVoid([&] { SetOf(set).Clear(); }); }
uint32_t koral_resource_set_image_count(KoralResourceSet* set)
{
    return Guarded([&] { return static_cast<uint32_t>(SetOf(set).Images().size()); }, 0u);
}
KoralImage* koral_resource_set_image(KoralResourceSet* set, const uint32_t index)
{
    return Guarded([&] {
        const auto& images = SetOf(set).Images();
        return index < images.size() ? Borrow(images[index]) : nullptr;
    }, static_cast<KoralResource*>(nullptr));
}
uint32_t koral_resource_set_buffer_count(KoralResourceSet* set)
{
    return Guarded([&] { return static_cast<uint32_t>(SetOf(set).Buffers().size()); }, 0u);
}
KoralBuffer* koral_resource_set_buffer(KoralResourceSet* set, const uint32_t index)
{
    return Guarded([&] {
        const auto& buffers = SetOf(set).Buffers();
        return index < buffers.size() ? Borrow(buffers[index]) : nullptr;
    }, static_cast<KoralResource*>(nullptr));
}
uint64_t koral_resource_set_generation(KoralResourceSet* set) { return Guarded([&] { return SetOf(set).Generation(); }, uint64_t { 0 }); }

KoralRenderPass* koral_graph_add_debug_draw_pass(KoralFrameGraph* graph, KoralDebugDraw* draw, void (*viewProjection)(float out[16], void* user),
                                                 void* user, void (*destroy)(void* user), const char* target, const char* depth)
{
    // Freed with the pass: the camera lambda is the only owner of @p user.
    std::shared_ptr<void> owned(user, [destroy](void* u) { if (destroy) destroy(u); });
    if (!draw) { Fail("koral_graph_add_debug_draw_pass needs a DebugDraw"); return nullptr; }
    return Guarded([&] {
        auto& pass = GraphOf(graph).Add<DebugDrawPass>(*reinterpret_cast<DebugDraw*>(draw), [viewProjection, owned] {
            kor::Mat4 matrix(1.f);
            if (viewProjection) viewProjection(&matrix[0][0], owned.get());
            return matrix;
        }, target ? std::string(target) : std::string(FrameGraph::Screen), depth ? std::string(depth) : std::string());
        return HandleOf(pass);
    }, static_cast<KoralRenderPass*>(nullptr));
}

} // extern "C"
