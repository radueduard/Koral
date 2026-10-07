//
// The C interface: command buffers.
//

#include <cstring>

#include "capi.h"

#include "commandBuffer.h"
#include "token.h"

using namespace kor;
using namespace kor::capi;

namespace
{
    CommandBuffer& CommandsOf(KoralCommandBuffer* commands)
    {
        if (!commands) throw std::runtime_error("no command buffer was given");
        return *reinterpret_cast<CommandBuffer*>(commands);
    }

    KoralCommandBuffer* HandleOf(CommandBuffer& commands) { return reinterpret_cast<KoralCommandBuffer*>(&commands); }

    // The untyped PushConstant and PushConstantBlock are protected: the typed templates in front of them
    // are what C++ calls. A binding has only bytes and a shape, so it reaches the untyped ones through
    // member pointers, which name them without needing to be a command buffer. Clang only grants protected
    // access when the member is named through the derived class, so the accessors must not reuse the names.
    struct Access : CommandBuffer {
        using PushConstantRaw = CommandBuffer& (CommandBuffer::*)(std::string_view, const void*, glm::u32, ValueShape,
                                                                   std::source_location);
        using PushBlockRaw = CommandBuffer& (CommandBuffer::*)(const void*, glm::u32, glm::u32);
        static PushConstantRaw RawPushConstant() { return &Access::PushConstant; }
        static PushBlockRaw RawPushConstantBlock() { return &Access::PushConstantBlock; }
    };

    template<typename Body>
    void Record(KoralCommandBuffer* commands, Body&& body)
    {
        GuardedVoid([&] { body(CommandsOf(commands)); });
    }

    glm::vec4 Vec4Of(const float* v) { return v ? glm::vec4(v[0], v[1], v[2], v[3]) : glm::vec4(1.f); }
    glm::ivec3 IVec3Of(const int32_t* v) { return {v[0], v[1], v[2]}; }

    Blit BlitOf(const KoralBlit* b)
    {
        Blit out;
        if (!b) return out;
        out.srcOffset = IVec3Of(b->src_offset);
        out.srcExtent = IVec3Of(b->src_extent);
        out.dstOffset = IVec3Of(b->dst_offset);
        out.dstExtent = IVec3Of(b->dst_extent);
        out.srcBaseArrayLayer = b->src_base_array_layer;
        out.dstBaseArrayLayer = b->dst_base_array_layer;
        out.layerCount = b->layer_count;
        out.srcMipLevel = b->src_mip_level;
        out.dstMipLevel = b->dst_mip_level;
        out.filtering = static_cast<Filter>(b->filtering);
        return out;
    }

    Resolve ResolveOf(const KoralResolveInfo* r)
    {
        Resolve out;
        if (!r) return out;
        out.srcOffset = IVec3Of(r->src_offset);
        out.srcExtent = IVec3Of(r->src_extent);
        out.dstOffset = IVec3Of(r->dst_offset);
        out.dstExtent = IVec3Of(r->dst_extent);
        out.srcBaseArrayLayer = r->src_base_array_layer;
        out.dstBaseArrayLayer = r->dst_base_array_layer;
        out.layerCount = r->layer_count;
        out.srcMipLevel = r->src_mip_level;
        out.dstMipLevel = r->dst_mip_level;
        return out;
    }

    Copy CopyOf(const KoralCopy* c)
    {
        Copy out;
        if (!c) return out;
        out.bufferOffset = c->buffer_offset;
        out.bufferRowLength = c->buffer_row_length;
        out.bufferImageHeight = c->buffer_image_height;
        out.imageOffset = IVec3Of(c->image_offset);
        out.imageExtent = IVec3Of(c->image_extent);
        out.imageBaseArrayLayer = c->image_base_array_layer;
        out.imageLayerCount = c->image_layer_count;
        out.imageMipLevel = c->image_mip_level;
        return out;
    }

    RenderInfo RenderInfoOf(const KoralRenderInfo* info)
    {
        if (!info) return {};
        RenderInfo out = info->framebuffer ? RenderInfo(RefOf<Framebuffer>(info->framebuffer)) : RenderInfo();
        out.SetColorLoadOperation(static_cast<LoadOperation>(info->color_load))
           .SetDepthLoadOperation(static_cast<LoadOperation>(info->depth_load))
           .SetStencilLoadOperation(static_cast<LoadOperation>(info->stencil_load))
           .SetColorStoreOperation(static_cast<StoreOperation>(info->color_store))
           .SetDepthStoreOperation(static_cast<StoreOperation>(info->depth_store))
           .SetStencilStoreOperation(static_cast<StoreOperation>(info->stencil_store));
        for (std::size_t i = 0; i < info->clear_color_count; ++i)
            if (info->clear_colors[i].components) out.SetClearColor(static_cast<glm::u32>(i), ClearColorOf(info->clear_colors[i]));
        if (info->has_clear_depth) out.SetClearDepth(info->clear_depth);
        if (info->has_clear_stencil) out.SetClearStencil(info->clear_stencil);
        return out;
    }

    std::optional<glm::u32> OptionalOf(const int64_t value)
    {
        return value < 0 ? std::nullopt : std::optional(static_cast<glm::u32>(value));
    }
}

extern "C" {

KoralCommandBuffer* koral_cmd_create(const uint32_t usage)
{
    return Guarded([&] { return HandleOf(*CommandBuffer::Create(FlagsOf<CommandBuffer::Usage>(usage)).release()); },
                   static_cast<KoralCommandBuffer*>(nullptr));
}
void koral_cmd_destroy(KoralCommandBuffer* commands)
{
    GuardedVoid([&] { delete reinterpret_cast<CommandBuffer*>(commands); });
}
KoralToken* koral_cmd_single_time_command(void (*record)(KoralCommandBuffer*, void*), void* user, const uint32_t usage)
{
    return Guarded([&] {
        return new KoralToken{CommandBuffer::SingleTimeCommand([&](CommandBuffer& commands) {
            if (record) record(HandleOf(commands), user);
        }, static_cast<CommandBuffer::Usage>(usage))};
    }, static_cast<KoralToken*>(nullptr));
}
void koral_cmd_begin(KoralCommandBuffer* c) { Record(c, [](auto& x) { x.Begin(); }); }
void koral_cmd_end(KoralCommandBuffer* c) { Record(c, [](auto& x) { x.End(); }); }
bool koral_cmd_is_recording(KoralCommandBuffer* c) { return Guarded([&] { return CommandsOf(c).IsRecording(); }, false); }
KoralStatus koral_cmd_submit(KoralCommandBuffer* c, KoralToken* const* waitFor, const size_t waitCount, KoralToken* const* signal,
                             const size_t signalCount)
{
    return Guarded([&] {
        SubmitInfo info;
        for (std::size_t i = 0; i < waitCount; ++i) if (waitFor[i]) info.waitFor.push_back(waitFor[i]->token);
        for (std::size_t i = 0; i < signalCount; ++i) if (signal[i]) info.signal.push_back(signal[i]->token);
        const auto submitted = CommandsOf(c).Submit(info);
        return submitted ? KORAL_OK : Fail(submitted.error().message);
    }, KORAL_ERROR);
}
void koral_cmd_reset(KoralCommandBuffer* c) { Record(c, [](auto& x) { x.Reset(); }); }
void koral_cmd_wait_for_fence(KoralCommandBuffer* c) { Record(c, [](auto& x) { x.WaitForFence(); }); }

bool koral_cmd_ok(KoralCommandBuffer* c) { return Guarded([&] { return CommandsOf(c).Ok(); }, false); }
uint32_t koral_cmd_error_count(KoralCommandBuffer* c) { return Guarded([&] { return static_cast<uint32_t>(CommandsOf(c).Errors().size()); }, 0u); }
const char* koral_cmd_error(KoralCommandBuffer* c, const uint32_t i)
{
    return Guarded([&] { return Keep(CommandsOf(c).Errors().at(i).History()); }, Keep(""));
}
bool koral_cmd_has_touched(KoralCommandBuffer* c, KoralImage* image)
{
    return Guarded([&] { return CommandsOf(c).HasTouched(RefOf<Image>(image)); }, false);
}
KoralImage* koral_cmd_screen_image(void)
{
    return Guarded([] { return Borrow(CommandBuffer::ScreenImage()); }, static_cast<KoralResource*>(nullptr));
}

void koral_cmd_begin_timer(KoralCommandBuffer* c, const char* label) { Record(c, [&](auto& x) { x.BeginTimer(label ? label : ""); }); }
void koral_cmd_end_timer(KoralCommandBuffer* c) { Record(c, [](auto& x) { x.EndTimer(); }); }
KoralStatus koral_cmd_collect_timer(KoralCommandBuffer* c, const char* label, double* milliseconds)
{
    return Guarded([&] {
        const auto result = CommandsOf(c).CollectTimer(label ? label : "");
        if (!result) return Fail(result.error().message);
        if (milliseconds) *milliseconds = *result;
        return KORAL_OK;
    }, KORAL_ERROR);
}
uint32_t koral_cmd_collect_timings(KoralCommandBuffer* c)
{
    return Guarded([&] { return static_cast<uint32_t>(CommandsOf(c).CollectTimings().size()); }, 0u);
}
const char* koral_cmd_timing(KoralCommandBuffer* c, const uint32_t i, double* milliseconds, uint32_t* depth)
{
    return Guarded([&] {
        const auto& timing = CommandsOf(c).Timings().at(i);
        if (milliseconds) *milliseconds = timing.milliseconds;
        if (depth) *depth = timing.depth;
        return Keep(timing.label);
    }, Keep(""));
}
bool koral_cmd_supports_timers(KoralCommandBuffer* c) { return Guarded([&] { return CommandsOf(c).SupportsTimers(); }, false); }
uint64_t koral_cmd_last_frame_command_count(KoralCommandBuffer* c)
{
    return Guarded([&] { return CommandsOf(c).LastFrameCommandCount(); }, glm::u64{0});
}

void koral_cmd_begin_rendering(KoralCommandBuffer* c, const KoralRenderInfo* info) { Record(c, [&](auto& x) { x.BeginRendering(RenderInfoOf(info)); }); }
void koral_cmd_end_rendering(KoralCommandBuffer* c) { Record(c, [](auto& x) { x.EndRendering(); }); }
void koral_cmd_set_viewport(KoralCommandBuffer* c, const uint32_t x_, const uint32_t y, const uint32_t w, const uint32_t h) { Record(c, [&](auto& x) { x.SetViewport(x_, y, w, h); }); }
void koral_cmd_set_scissor(KoralCommandBuffer* c, const uint32_t x_, const uint32_t y, const uint32_t w, const uint32_t h) { Record(c, [&](auto& x) { x.SetScissor(x_, y, w, h); }); }
void koral_cmd_set_line_width(KoralCommandBuffer* c, const float w) { Record(c, [&](auto& x) { x.SetLineWidth(w); }); }
void koral_cmd_set_depth_bias(KoralCommandBuffer* c, const float k, const float clamp, const float slope) { Record(c, [&](auto& x) { x.SetDepthBias(k, clamp, slope); }); }
void koral_cmd_set_blend_constants(KoralCommandBuffer* c, const float k[4]) { Record(c, [&](auto& x) { x.SetBlendConstants(Vec4Of(k)); }); }
void koral_cmd_set_stencil_compare_mask(KoralCommandBuffer* c, const uint32_t f, const uint32_t m) { Record(c, [&](auto& x) { x.SetStencilCompareMask(static_cast<StencilFace>(f), m); }); }
void koral_cmd_set_stencil_write_mask(KoralCommandBuffer* c, const uint32_t f, const uint32_t m) { Record(c, [&](auto& x) { x.SetStencilWriteMask(static_cast<StencilFace>(f), m); }); }
void koral_cmd_set_stencil_reference(KoralCommandBuffer* c, const uint32_t f, const uint32_t r) { Record(c, [&](auto& x) { x.SetStencilReference(static_cast<StencilFace>(f), r); }); }
void koral_cmd_set_cull_mode(KoralCommandBuffer* c, const uint32_t m) { Record(c, [&](auto& x) { x.SetCullMode(FlagsOf<CullMode>(m)); }); }
void koral_cmd_set_front_face(KoralCommandBuffer* c, const uint32_t f) { Record(c, [&](auto& x) { x.SetFrontFace(static_cast<FrontFace>(f)); }); }
void koral_cmd_set_depth_test_enable(KoralCommandBuffer* c, const bool e) { Record(c, [&](auto& x) { x.SetDepthTestEnable(e); }); }
void koral_cmd_set_depth_write_enable(KoralCommandBuffer* c, const bool e) { Record(c, [&](auto& x) { x.SetDepthWriteEnable(e); }); }
void koral_cmd_set_depth_compare_op(KoralCommandBuffer* c, const uint32_t op) { Record(c, [&](auto& x) { x.SetDepthCompareOp(static_cast<CompareOp>(op)); }); }
void koral_cmd_set_stencil_test_enable(KoralCommandBuffer* c, const bool e) { Record(c, [&](auto& x) { x.SetStencilTestEnable(e); }); }
void koral_cmd_set_stencil_op(KoralCommandBuffer* c, const uint32_t face, const uint32_t fail, const uint32_t pass, const uint32_t depthFail,
                              const uint32_t compare)
{
    Record(c, [&](auto& x) {
        x.SetStencilOp(static_cast<StencilFace>(face), static_cast<StencilOp>(fail), static_cast<StencilOp>(pass),
                       static_cast<StencilOp>(depthFail), static_cast<CompareOp>(compare));
    });
}
void koral_cmd_set_depth_bias_enable(KoralCommandBuffer* c, const bool e) { Record(c, [&](auto& x) { x.SetDepthBiasEnable(e); }); }
void koral_cmd_set_rasterizer_discard_enable(KoralCommandBuffer* c, const bool e) { Record(c, [&](auto& x) { x.SetRasterizerDiscardEnable(e); }); }
void koral_cmd_set_primitive_restart_enable(KoralCommandBuffer* c, const bool e) { Record(c, [&](auto& x) { x.SetPrimitiveRestartEnable(e); }); }
void koral_cmd_bind_compute_pipeline(KoralCommandBuffer* c, KoralComputePipeline* p) { Record(c, [&](auto& x) { x.BindComputePipeline(RefOf<ComputePipeline>(p)); }); }
void koral_cmd_bind_graphics_pipeline(KoralCommandBuffer* c, KoralGraphicsPipeline* p) { Record(c, [&](auto& x) { x.BindGraphicsPipeline(RefOf<GraphicsPipeline>(p)); }); }
void koral_cmd_bind_ray_tracing_pipeline(KoralCommandBuffer* c, KoralRayTracingPipeline* p) { Record(c, [&](auto& x) { x.BindRayTracingPipeline(RefOf<RayTracingPipeline>(p)); }); }
void koral_cmd_bind_descriptor_set(KoralCommandBuffer* c, const uint32_t i, KoralDescriptorSet* s) { Record(c, [&](auto& x) { x.BindDescriptorSet(i, RefOf<DescriptorSet>(s)); }); }
void koral_cmd_bind_mesh(KoralCommandBuffer* c, KoralMesh* m) { Record(c, [&](auto& x) { x.BindMesh(RefOf<Mesh>(m)); }); }
void koral_cmd_bind_vertex_buffer(KoralCommandBuffer* c, const uint32_t binding, KoralBuffer* b, const uint64_t offset)
{
    Record(c, [&](auto& x) { x.BindVertexBuffer(binding, RefOf<Buffer>(b), offset); });
}
void koral_cmd_push_constant_block(KoralCommandBuffer* c, const void* data, const uint32_t bytes, const uint32_t offset)
{
    Record(c, [&](CommandBuffer& x) { (x.*Access::RawPushConstantBlock())(data, bytes, offset); });
}
void koral_cmd_push_constant(KoralCommandBuffer* c, const char* name, const void* data, const uint32_t bytes, const KoralValueShape* shape)
{
    Record(c, [&](CommandBuffer& x) {
        ValueShape s;
        if (shape) {
            s.scalar = static_cast<ValueScalar>(shape->scalar);
            s.rows = static_cast<std::uint8_t>(shape->rows);
            s.columns = static_cast<std::uint8_t>(shape->columns);
            s.count = shape->count;
            s.known = shape->known;
        }
        (x.*Access::RawPushConstant())(name ? name : "", data, bytes, s, std::source_location::current());
    });
}
void koral_cmd_barrier(KoralCommandBuffer* c, const KoralBufferBarrier* buffers, const size_t bufferCount, const KoralImageBarrier* images,
                       const size_t imageCount)
{
    Record(c, [&](auto& x) {
        std::vector<BufferBarrier> bufferBarriers;
        std::vector<ImageBarrier> imageBarriers;
        for (std::size_t i = 0; i < bufferCount; ++i)
            bufferBarriers.emplace_back(RefOf<Buffer>(buffers[i].buffer), static_cast<ResourceAccess>(buffers[i].dst_access),
                                        buffers[i].offset, buffers[i].size);
        for (std::size_t i = 0; i < imageCount; ++i)
            imageBarriers.emplace_back(RefOf<Image>(images[i].image), static_cast<ResourceAccess>(images[i].dst_access),
                                       OptionalOf(images[i].base_mip_level), OptionalOf(images[i].level_count),
                                       OptionalOf(images[i].base_array_layer), OptionalOf(images[i].layer_count));
        x.Barrier(std::move(bufferBarriers), std::move(imageBarriers));
    });
}
void koral_cmd_begin_debug_label(KoralCommandBuffer* c, const char* label, const float color[4]) { Record(c, [&](auto& x) { x.BeginDebugLabel(label ? label : "", Vec4Of(color)); }); }
void koral_cmd_end_debug_label(KoralCommandBuffer* c) { Record(c, [](auto& x) { x.EndDebugLabel(); }); }
void koral_cmd_insert_debug_label(KoralCommandBuffer* c, const char* label, const float color[4]) { Record(c, [&](auto& x) { x.InsertDebugLabel(label ? label : "", Vec4Of(color)); }); }
void koral_cmd_dispatch(KoralCommandBuffer* c, const uint32_t x_, const uint32_t y, const uint32_t z) { Record(c, [&](auto& x) { x.Dispatch(x_, y, z); }); }
void koral_cmd_dispatch_indirect(KoralCommandBuffer* c, KoralBuffer* b, const uint64_t o) { Record(c, [&](auto& x) { x.DispatchIndirect(RefOf<Buffer>(b), o); }); }
void koral_cmd_trace_rays(KoralCommandBuffer* c, const uint32_t w, const uint32_t h, const uint32_t d) { Record(c, [&](auto& x) { x.TraceRays(w, h, d); }); }
void koral_cmd_draw(KoralCommandBuffer* c, const uint64_t vertices, const uint32_t instances, const uint32_t firstVertex, const uint32_t firstInstance)
{
    Record(c, [&](auto& x) { x.Draw(vertices, instances, firstVertex, firstInstance); });
}
void koral_cmd_draw_indexed(KoralCommandBuffer* c, const uint64_t indices, const uint32_t instances, const uint32_t firstIndex, const int32_t vertexOffset,
                            const uint32_t firstInstance)
{
    Record(c, [&](auto& x) { x.DrawIndexed(indices, instances, firstIndex, vertexOffset, firstInstance); });
}
void koral_cmd_draw_mesh(KoralCommandBuffer* c, KoralMesh* m, const uint32_t instances, const uint32_t base) { Record(c, [&](auto& x) { x.DrawMesh(RefOf<Mesh>(m), instances, base); }); }
void koral_cmd_draw_sub_mesh(KoralCommandBuffer* c, KoralMesh* m, const uint32_t base, const uint32_t count) { Record(c, [&](auto& x) { x.DrawSubMesh(RefOf<Mesh>(m), base, count); }); }
void koral_cmd_draw_mesh_tasks(KoralCommandBuffer* c, const uint32_t x_, const uint32_t y, const uint32_t z) { Record(c, [&](auto& x) { x.DrawMeshTasks(x_, y, z); }); }
void koral_cmd_draw_indirect(KoralCommandBuffer* c, KoralBuffer* b, const uint64_t o, const uint32_t n, const uint32_t s) { Record(c, [&](auto& x) { x.DrawIndirect(RefOf<Buffer>(b), o, n, s); }); }
void koral_cmd_draw_indexed_indirect(KoralCommandBuffer* c, KoralBuffer* b, const uint64_t o, const uint32_t n, const uint32_t s) { Record(c, [&](auto& x) { x.DrawIndexedIndirect(RefOf<Buffer>(b), o, n, s); }); }
void koral_cmd_draw_mesh_tasks_indirect(KoralCommandBuffer* c, KoralBuffer* b, const uint64_t o, const uint32_t n, const uint32_t s) { Record(c, [&](auto& x) { x.DrawMeshTasksIndirect(RefOf<Buffer>(b), o, n, s); }); }
void koral_cmd_clear_buffer(KoralCommandBuffer* c, KoralBuffer* b, const uint64_t o, const uint64_t n) { Record(c, [&](auto& x) { x.ClearBuffer(RefOf<Buffer>(b), o, n); }); }
void koral_cmd_clear_color_image(KoralCommandBuffer* c, KoralImage* i, const float color[4]) { Record(c, [&](auto& x) { x.ClearColorImage(RefOf<Image>(i), Vec4Of(color)); }); }
void koral_cmd_fill_buffer(KoralCommandBuffer* c, KoralBuffer* b, const void* data, const uint64_t o, const uint64_t n) { Record(c, [&](auto& x) { x.FillBuffer(RefOf<Buffer>(b), data, o, n); }); }
void koral_cmd_copy_buffer(KoralCommandBuffer* c, KoralBuffer* s, KoralBuffer* d, const uint64_t n, const uint64_t so, const uint64_t dO)
{
    Record(c, [&](auto& x) { x.CopyBuffer(RefOf<Buffer>(s), RefOf<Buffer>(d), n, so, dO); });
}
void koral_cmd_blit_to_screen(KoralCommandBuffer* c, KoralImage* s, const KoralBlit* b) { Record(c, [&](auto& x) { x.BlitToScreen(RefOf<Image>(s), BlitOf(b)); }); }
void koral_cmd_blit(KoralCommandBuffer* c, KoralImage* s, KoralImage* d, const KoralBlit* b) { Record(c, [&](auto& x) { x.Blit(RefOf<Image>(s), RefOf<Image>(d), BlitOf(b)); }); }
void koral_cmd_copy_image(KoralCommandBuffer* c, KoralImage* s, KoralImage* d) { Record(c, [&](auto& x) { x.CopyImage(RefOf<Image>(s), RefOf<Image>(d)); }); }
void koral_cmd_resolve_to_screen(KoralCommandBuffer* c, KoralImage* s, const KoralResolveInfo* r) { Record(c, [&](auto& x) { x.ResolveToScreen(RefOf<Image>(s), ResolveOf(r)); }); }
void koral_cmd_resolve(KoralCommandBuffer* c, KoralImage* s, KoralImage* d, const KoralResolveInfo* r) { Record(c, [&](auto& x) { x.Resolve(RefOf<Image>(s), RefOf<Image>(d), ResolveOf(r)); }); }
void koral_cmd_generate_mipmaps(KoralCommandBuffer* c, KoralImage* i) { Record(c, [&](auto& x) { x.GenerateMipmaps(RefOf<Image>(i)); }); }
void koral_cmd_copy_buffer_to_image(KoralCommandBuffer* c, KoralBuffer* b, KoralImage* i, const KoralCopy* copy)
{
    Record(c, [&](auto& x) { x.CopyBufferToImage(RefOf<Buffer>(b), RefOf<Image>(i), CopyOf(copy)); });
}
void koral_cmd_copy_image_to_buffer(KoralCommandBuffer* c, KoralImage* i, KoralBuffer* b, const KoralCopy* copy)
{
    Record(c, [&](auto& x) { x.CopyImageToBuffer(RefOf<Image>(i), RefOf<Buffer>(b), CopyOf(copy)); });
}
void koral_cmd_run(KoralCommandBuffer* c, void (*command)(KoralCommandBuffer*, void*), void* user)
{
    Record(c, [&](auto& x) { x.Run([command, user](CommandBuffer& inner) { if (command) command(HandleOf(inner), user); }); });
}

} // extern "C"
