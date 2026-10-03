//
// The C interface: resources, their builders, and tokens.
//

#include <cstring>
#include <span>

#include "capi.h"
#include "capiInterop.h"

#include "token.h"

namespace kor::capi
{
    namespace
    {
        thread_local std::string t_error;
        thread_local std::string t_string;
    }

    std::string& LastError() { return t_error; }

    const char* Keep(std::string text)
    {
        t_string = std::move(text);
        return t_string.c_str();
    }

    KoralStatus Fail(std::string message)
    {
        t_error = std::move(message);
        return KORAL_ERROR;
    }

    const char* KindName(const KoralResourceKind kind)
    {
        switch (kind) {
        case KORAL_RESOURCE_BUFFER: return "buffer";
        case KORAL_RESOURCE_IMAGE: return "image";
        case KORAL_RESOURCE_IMAGE_VIEW: return "image view";
        case KORAL_RESOURCE_SAMPLER: return "sampler";
        case KORAL_RESOURCE_BUFFER_VIEW: return "buffer view";
        case KORAL_RESOURCE_SHADER: return "shader";
        case KORAL_RESOURCE_GRAPHICS_PIPELINE: return "graphics pipeline";
        case KORAL_RESOURCE_COMPUTE_PIPELINE: return "compute pipeline";
        case KORAL_RESOURCE_RAY_TRACING_PIPELINE: return "ray tracing pipeline";
        case KORAL_RESOURCE_DESCRIPTOR_SET: return "descriptor set";
        case KORAL_RESOURCE_DESCRIPTOR_SET_LAYOUT: return "descriptor set layout";
        case KORAL_RESOURCE_FRAMEBUFFER: return "framebuffer";
        case KORAL_RESOURCE_MESH: return "mesh";
        case KORAL_RESOURCE_ACCELERATION_STRUCTURE: return "acceleration structure";
        }
        return "resource";
    }

    void ThrowUnusable(const KoralResource& resource)
    {
        const auto name = resource.Name();
        const auto label = std::string(KindName(resource.Kind())) + (name.empty() ? "" : " '" + name + "'");
        if (const auto* failure = resource.Failure())
            throw std::runtime_error("the " + label + " is unusable:\n" + failure->History());
        throw std::runtime_error("the " + label + " is gone");
    }

    ResourceRef<const Pipeline> PipelineOf(KoralResource* resource)
    {
        if (!resource) throw std::runtime_error("no pipeline was given");
        switch (resource->Kind()) {
        case KORAL_RESOURCE_GRAPHICS_PIPELINE: return RefOf<GraphicsPipeline>(resource);
        case KORAL_RESOURCE_COMPUTE_PIPELINE: return RefOf<ComputePipeline>(resource);
        case KORAL_RESOURCE_RAY_TRACING_PIPELINE: return RefOf<RayTracingPipeline>(resource);
        default: throw std::runtime_error(std::string("expected a pipeline, but was given a ") + KindName(resource->Kind()));
        }
    }

    ClearColor ClearColorOf(const KoralClearColor& c)
    {
        const auto& f = c.value.f;
        const auto& i = c.value.i;
        const auto& u = c.value.u;
        switch (c.scalar_type * 4 + (c.components ? c.components - 1 : 3)) {
        case 0: return f[0];
        case 1: return glm::vec2(f[0], f[1]);
        case 2: return glm::vec3(f[0], f[1], f[2]);
        case 3: return glm::vec4(f[0], f[1], f[2], f[3]);
        case 4: return i[0];
        case 5: return glm::ivec2(i[0], i[1]);
        case 6: return glm::ivec3(i[0], i[1], i[2]);
        case 7: return glm::ivec4(i[0], i[1], i[2], i[3]);
        case 8: return u[0];
        case 9: return glm::uvec2(u[0], u[1]);
        case 10: return glm::uvec3(u[0], u[1], u[2]);
        case 11: return glm::uvec4(u[0], u[1], u[2], u[3]);
        default: throw std::runtime_error("a clear colour's scalar type is 0 (float), 1 (int) or 2 (uint)");
        }
    }

    KoralClearColor ClearColorOf(const ClearColor& color)
    {
        KoralClearColor out{};
        std::visit([&]<typename V>(const V& value) {
            if constexpr (std::is_same_v<V, float>) { out.scalar_type = 0; out.components = 1; out.value.f[0] = value; }
            else if constexpr (std::is_same_v<V, glm::i32>) { out.scalar_type = 1; out.components = 1; out.value.i[0] = value; }
            else if constexpr (std::is_same_v<V, glm::u32>) { out.scalar_type = 2; out.components = 1; out.value.u[0] = value; }
            else {
                using S = typename V::value_type;
                out.scalar_type = std::is_same_v<S, float> ? 0 : std::is_signed_v<S> ? 1 : 2;
                out.components = static_cast<std::uint32_t>(V::length());
                for (glm::length_t k = 0; k < V::length(); ++k) {
                    if constexpr (std::is_same_v<S, float>) out.value.f[k] = value[k];
                    else if constexpr (std::is_signed_v<S>) out.value.i[k] = value[k];
                    else out.value.u[k] = value[k];
                }
            }
        }, color);
        return out;
    }

    VertexLayout VertexLayoutOf(const KoralVertexLayout& layout)
    {
        VertexLayout out;
        for (std::size_t i = 0; i < layout.binding_count; ++i)
            out.bindings.push_back({layout.bindings[i].binding, layout.bindings[i].stride});
        for (std::size_t i = 0; i < layout.attribute_count; ++i) {
            const auto& a = layout.attributes[i];
            VertexLayout::Attribute attribute{
                .semantic = a.semantic ? a.semantic : "",
                .semanticNamespace = a.semantic_namespace ? a.semantic_namespace : "",
                .binding = a.binding,
                .offset = a.offset,
                .channelType = static_cast<ChannelType>(a.channel_type),
                .channelCount = a.channel_count,
            };
            if (a.location >= 0) attribute.location = static_cast<glm::u32>(a.location);
            out.attributes.push_back(std::move(attribute));
        }
        if (layout.position_attribute >= 0) out.positionAttribute = static_cast<std::size_t>(layout.position_attribute);
        return out;
    }
}

using namespace kor;
using namespace kor::capi;

struct KoralReadback {
    Resource<Buffer> staging;          // device-local: where the GPU copies it to
    std::vector<std::byte> bytes;      // host-visible: read at once
    glm::u64 size = 0;
};

struct KoralMapping {
    std::optional<Buffer::MutableMapping<std::byte>> mutableMapping;
    std::optional<Buffer::ConstMapping<std::byte>> constMapping;
    std::byte* data = nullptr;
    glm::u64 size = 0;
};

extern "C" {

// ---- results and the log -------------------------------------------------------------------------------

const char* koral_last_error(void) { return LastError().c_str(); }
void koral_set_last_error(const char* message) { LastError() = message ? message : ""; }


void koral_log(const KoralLogLevel level, const char* message)
{
    const std::string text = message ? message : "";
    switch (level) {
    case KORAL_LOG_WARN: log::Warn("{}", text); break;
    case KORAL_LOG_ERROR: log::Error("{}", text); break;
    default: log::Info("{}", text); break;
    }
}

const char* koral_error_describe(const uint32_t code) { return Keep(std::string(Describe(static_cast<ErrorCode>(code)))); }

// ---- resources -------------------------------------------------------------------------------------------

void koral_resource_release(KoralResource* resource) { GuardedVoid([&] { delete resource; }); }
KoralResource* koral_resource_borrow(KoralResource* resource) { return resource ? resource->Borrow() : nullptr; }
KoralResourceKind koral_resource_kind(KoralResource* resource) { return resource ? resource->Kind() : static_cast<KoralResourceKind>(0); }
bool koral_resource_owned(KoralResource* resource) { return resource && resource->Owned(); }
bool koral_resource_alive(KoralResource* resource) { return resource && resource->Alive(); }
bool koral_resource_valid(KoralResource* resource) { return resource && resource->Valid(); }
bool koral_resource_poisoned(KoralResource* resource) { return resource && resource->Poisoned(); }
const char* koral_resource_name(KoralResource* resource) { return Keep(resource ? resource->Name() : std::string()); }
void koral_resource_set_name(KoralResource* resource, const char* name)
{
    if (resource) GuardedVoid([&] { resource->SetName(name ? name : ""); });
}
bool koral_resource_retry(KoralResource* resource) { return resource && Guarded([&] { return resource->Retry(); }, false); }
const void* koral_resource_identity(KoralResource* resource) { return resource ? resource->Identity() : nullptr; }
uint32_t koral_resource_error_code(KoralResource* resource)
{
    const auto* failure = resource ? resource->Failure() : nullptr;
    return failure ? static_cast<uint32_t>(failure->code) : 0;
}
const char* koral_resource_error_message(KoralResource* resource)
{
    const auto* failure = resource ? resource->Failure() : nullptr;
    return Keep(failure ? failure->message : std::string());
}
const char* koral_resource_error_history(KoralResource* resource)
{
    const auto* failure = resource ? resource->Failure() : nullptr;
    return Keep(failure ? failure->History() : std::string());
}

void koral_builder_destroy(KoralBuilder* builder) { delete builder; }
bool koral_builder_has_errors(KoralBuilder* builder) { return builder && builder->HasErrors(); }

// ---- Buffer ------------------------------------------------------------------------------------------------

using BufferBuilder = Buffer::Builder<std::byte>;

KoralBufferBuilder* koral_buffer_builder_new(void) { return new TypedBuilder<BufferBuilder>(); }
void koral_buffer_builder_set_instance_count(KoralBufferBuilder* b, const int64_t bytes)
{
    Set<BufferBuilder>(b, [&](auto& x) { x.SetInstanceCount(bytes); });
}
void koral_buffer_builder_set_data(KoralBufferBuilder* b, const void* data, const uint64_t bytes)
{
    Set<BufferBuilder>(b, [&](auto& x) {
        const auto* p = static_cast<const std::byte*>(data);
        x.SetData(std::span<const std::byte>(p, data ? static_cast<std::size_t>(bytes) : 0));
    });
}
void koral_buffer_builder_set_usage(KoralBufferBuilder* b, const uint32_t usage)
{
    Set<BufferBuilder>(b, [&](auto& x) { x.SetUsage(FlagsOf<Buffer::Usage>(usage)); });
}
void koral_buffer_builder_set_type(KoralBufferBuilder* b, const uint32_t type)
{
    Set<BufferBuilder>(b, [&](auto& x) { x.SetType(static_cast<Buffer::Type>(type)); });
}
void koral_buffer_builder_set_is_per_frame(KoralBufferBuilder* b, const bool value)
{
    Set<BufferBuilder>(b, [&](auto& x) { x.SetIsPerFrame(value); });
}
void koral_buffer_builder_set_shared_across_queues(KoralBufferBuilder* b, const bool shared)
{
    Set<BufferBuilder>(b, [&](auto& x) { x.SetSharedAcrossQueues(shared); });
}
KoralBuffer* koral_buffer_builder_build(KoralBufferBuilder* b) { return Build<BufferBuilder>(b, [](auto& x) { return x.Build(); }); }

uint64_t koral_buffer_size(KoralBuffer* r) { return Guarded([&] { return Get<Buffer>(r).size(); }, glm::u64{0}); }
uint32_t koral_buffer_usage_flags(KoralBuffer* r) { return Guarded([&] { return BitsOf(Get<Buffer>(r).UsageFlags()); }, 0u); }
uint32_t koral_buffer_memory_type(KoralBuffer* r) { return Guarded([&] { return static_cast<uint32_t>(Get<Buffer>(r).MemoryType()); }, 0u); }
bool koral_buffer_is_host_visible(KoralBuffer* r) { return Guarded([&] { return Get<Buffer>(r).IsHostVisible(); }, false); }
bool koral_buffer_is_per_frame(KoralBuffer* r) { return Guarded([&] { return Get<Buffer>(r).IsPerFrame(); }, false); }
bool koral_buffer_is_shared_across_queues(KoralBuffer* r) { return Guarded([&] { return Get<Buffer>(r).IsSharedAcrossQueues(); }, false); }
uint64_t koral_buffer_device_address(KoralBuffer* r) { return Guarded([&] { return Get<Buffer>(r).DeviceAddress(); }, glm::u64{0}); }
uint32_t koral_buffer_copy_count(KoralBuffer* r) { return Guarded([&] { return Get<Buffer>(r).CopyCount(); }, 0u); }

KoralStatus koral_buffer_read(KoralBuffer* r, void* into, const uint64_t bytes, const uint64_t offset)
{
    if (!into && bytes) return Fail("koral_buffer_read needs somewhere to put what it reads");
    return Guarded([&] {
        const auto read = Get<Buffer>(r).Read<std::byte>(bytes, offset);
        std::memcpy(into, read.data(), read.size());
        return KORAL_OK;
    }, KORAL_ERROR);
}

KoralStatus koral_buffer_write(KoralBuffer* r, const void* data, const uint64_t bytes, const uint64_t offset)
{
    if (!data && bytes) return Fail("koral_buffer_write needs data");
    return Guarded([&] {
        const auto* p = static_cast<const std::byte*>(data);
        GetWritable<Buffer>(r).Write(std::span<const std::byte>(p, static_cast<std::size_t>(bytes)), offset);
        return KORAL_OK;
    }, KORAL_ERROR);
}

KoralReadback* koral_buffer_read_async(KoralBuffer* r, const uint64_t bytes, const uint64_t offset, KoralToken** done)
{
    return Guarded([&]() -> KoralReadback* {
        const auto& buffer = Get<Buffer>(r);
        auto readback = std::make_unique<KoralReadback>();
        readback->size = bytes;
        Token copied = Token::Create();
        if (buffer.MemoryType() != Buffer::Type::eDeviceLocal) {
            readback->bytes = buffer.Read<std::byte>(bytes, offset);   // memory the CPU sees: nothing to wait for
            copied.Signal();
        } else {
            readback->staging = Buffer::Builder<std::byte>()
                .SetInstanceCount(static_cast<glm::i64>(bytes))
                .SetUsage(Buffer::Usage::eTransferDst)
                .SetType(Buffer::Type::eReadback)
                .Build();
            copied = CommandBuffer::SingleTimeCommand([&](CommandBuffer& commandBuffer) {
                commandBuffer.CopyBuffer(RefOf<Buffer>(r), readback->staging, bytes, offset, 0);
            }, CommandBuffer::Usage::eTransfer);
        }
        if (done) *done = new KoralToken{copied};
        return readback.release();
    }, static_cast<KoralReadback*>(nullptr));
}

KoralStatus koral_readback_read(KoralReadback* readback, void* into)
{
    if (!readback || (!into && readback->size)) return Fail("koral_readback_read needs a readback and somewhere to put it");
    return Guarded([&] {
        if (readback->staging.Valid()) {
            const auto bytes = readback->staging->Read<std::byte>(readback->size, 0);
            std::memcpy(into, bytes.data(), bytes.size());
        } else {
            std::memcpy(into, readback->bytes.data(), readback->bytes.size());
        }
        return KORAL_OK;
    }, KORAL_ERROR);
}

void koral_readback_destroy(KoralReadback* readback) { GuardedVoid([&] { delete readback; }); }

KoralMapping* koral_buffer_map(KoralBuffer* r, const uint64_t bytes, const uint64_t offset, const bool mutable_)
{
    return Guarded([&]() -> KoralMapping* {
        auto mapping = std::make_unique<KoralMapping>();
        if (mutable_) {
            auto& buffer = GetWritable<Buffer>(r);
            mapping->mutableMapping.emplace(buffer.Map<std::byte>(bytes, offset));
            const auto span = mapping->mutableMapping->AsSpan();
            mapping->data = span.data();
            mapping->size = span.size();
        } else {
            const auto& buffer = Get<Buffer>(r);
            mapping->constMapping.emplace(buffer.Map<std::byte>(bytes, offset));
            const auto span = mapping->constMapping->AsSpan();
            mapping->data = const_cast<std::byte*>(span.data());
            mapping->size = span.size();
        }
        return mapping.release();
    }, static_cast<KoralMapping*>(nullptr));
}

void koral_mapping_release(KoralMapping* mapping) { GuardedVoid([&] { delete mapping; }); }
void* koral_mapping_data(KoralMapping* mapping) { return mapping ? mapping->data : nullptr; }
uint64_t koral_mapping_size(KoralMapping* mapping) { return mapping ? mapping->size : 0; }

KoralStatus koral_mapping_write(KoralMapping* mapping, const void* data, const uint64_t bytes, const uint64_t offset)
{
    if (!mapping || !mapping->mutableMapping) return Fail("only a mutable mapping can be written");
    return Guarded([&] {
        const auto* p = static_cast<const std::byte*>(data);
        mapping->mutableMapping->Write(std::span<const std::byte>(p, static_cast<std::size_t>(bytes)), offset);
        return KORAL_OK;
    }, KORAL_ERROR);
}

KoralStatus koral_mapping_flush(KoralMapping* mapping, const uint64_t offset, const uint64_t bytes)
{
    if (!mapping || !mapping->mutableMapping) return Fail("only a mutable mapping is flushed");
    return Guarded([&] { mapping->mutableMapping->Flush(offset, bytes); return KORAL_OK; }, KORAL_ERROR);
}

KoralStatus koral_mapping_invalidate(KoralMapping* mapping, const uint64_t offset, const uint64_t bytes)
{
    if (!mapping) return Fail("no mapping");
    return Guarded([&] {
        if (mapping->mutableMapping) mapping->mutableMapping->Invalidate(offset, bytes);
        else mapping->constMapping->Invalidate(offset, bytes);
        return KORAL_OK;
    }, KORAL_ERROR);
}

// ---- Image --------------------------------------------------------------------------------------------------

KoralImageBuilder* koral_image_builder_new(void) { return new TypedBuilder<Image::Builder>(); }
void koral_image_builder_set_is_per_frame(KoralImageBuilder* b, const bool v) { Set<Image::Builder>(b, [&](auto& x) { x.SetIsPerFrame(v); }); }
void koral_image_builder_set_shared_across_queues(KoralImageBuilder* b, const bool v) { Set<Image::Builder>(b, [&](auto& x) { x.SetSharedAcrossQueues(v); }); }
void koral_image_builder_set_type(KoralImageBuilder* b, const uint32_t v) { Set<Image::Builder>(b, [&](auto& x) { x.SetType(static_cast<Image::Type>(v)); }); }
void koral_image_builder_set_format(KoralImageBuilder* b, const uint32_t v) { Set<Image::Builder>(b, [&](auto& x) { x.SetFormat(static_cast<Image::Format>(v)); }); }
void koral_image_builder_set_extent(KoralImageBuilder* b, const uint32_t x_, const uint32_t y, const uint32_t z)
{
    Set<Image::Builder>(b, [&](auto& x) { x.SetExtent(glm::uvec3(x_, y, z)); });
}
void koral_image_builder_set_mip_levels(KoralImageBuilder* b, const uint32_t v) { Set<Image::Builder>(b, [&](auto& x) { x.SetMipLevels(v); }); }
void koral_image_builder_set_array_layers(KoralImageBuilder* b, const uint32_t v) { Set<Image::Builder>(b, [&](auto& x) { x.SetArrayLayers(v); }); }
void koral_image_builder_set_sample_count(KoralImageBuilder* b, const uint32_t v) { Set<Image::Builder>(b, [&](auto& x) { x.SetSampleCount(static_cast<SampleCount>(v)); }); }
void koral_image_builder_set_usage(KoralImageBuilder* b, const uint32_t v) { Set<Image::Builder>(b, [&](auto& x) { x.SetUsage(FlagsOf<Image::Usage>(v)); }); }
void koral_image_builder_set_data(KoralImageBuilder* b, const void* pixels, const uint64_t bytes)
{
    Set<Image::Builder>(b, [&](auto& x) { x.SetData(pixels, bytes); });
}
KoralImage* koral_image_builder_build(KoralImageBuilder* b) { return Build<Image::Builder>(b, [](auto& x) { return x.Build(); }); }

void koral_image_resize(KoralImage* r, const uint32_t x, const uint32_t y, const uint32_t z)
{
    GuardedVoid([&] { const_cast<Image&>(Get<Image>(r)).Resize({x, y, z}); });
}
uint64_t koral_image_generation(KoralImage* r) { return Guarded([&] { return Get<Image>(r).Generation(); }, glm::u64{0}); }
void koral_image_extent(KoralImage* r, uint32_t* x, uint32_t* y, uint32_t* z)
{
    const auto e = Guarded([&] { return Get<Image>(r).Extent(); }, glm::uvec3(0));
    if (x) *x = e.x;
    if (y) *y = e.y;
    if (z) *z = e.z;
}
uint32_t koral_image_image_type(KoralImage* r) { return Guarded([&] { return static_cast<uint32_t>(Get<Image>(r).ImageType()); }, 0u); }
uint32_t koral_image_pixel_format(KoralImage* r) { return Guarded([&] { return static_cast<uint32_t>(Get<Image>(r).PixelFormat()); }, 0u); }
bool koral_image_is_bgr_order(KoralImage* r) { return Guarded([&] { return Get<Image>(r).IsBgrOrder(); }, false); }
uint32_t koral_image_samples(KoralImage* r) { return Guarded([&] { return static_cast<uint32_t>(Get<Image>(r).Samples()); }, 0u); }
uint32_t koral_image_usage_flags(KoralImage* r) { return Guarded([&] { return BitsOf(Get<Image>(r).UsageFlags()); }, 0u); }
uint32_t koral_image_mip_levels(KoralImage* r) { return Guarded([&] { return Get<Image>(r).MipLevels(); }, 0u); }
uint32_t koral_image_array_layers(KoralImage* r) { return Guarded([&] { return Get<Image>(r).ArrayLayers(); }, 0u); }
bool koral_image_is_per_frame(KoralImage* r) { return Guarded([&] { return Get<Image>(r).IsPerFrame(); }, false); }
bool koral_image_is_shared_across_queues(KoralImage* r) { return Guarded([&] { return Get<Image>(r).IsSharedAcrossQueues(); }, false); }
uint32_t koral_image_copy_index(KoralImage* r) { return Guarded([&] { return Get<Image>(r).CopyIndex(); }, 0u); }
uint32_t koral_image_natural_shape(KoralImage* r) { return Guarded([&] { return static_cast<uint32_t>(Get<Image>(r).NaturalShape()); }, 0u); }
KoralImageView* koral_image_view(KoralImage* r, const uint32_t shape, const uint32_t coverage)
{
    return Guarded([&] {
        return Borrow(Get<Image>(r).View(static_cast<ImageShape>(shape), static_cast<Image::ViewCoverage>(coverage)));
    }, static_cast<KoralResource*>(nullptr));
}
uint32_t koral_image_channel_size(const uint32_t f) { return Guarded([&] { return Image::ChannelSize(static_cast<Image::Format>(f)); }, 0u); }
uint32_t koral_image_channel_count(const uint32_t f) { return Guarded([&] { return Image::ChannelCount(static_cast<Image::Format>(f)); }, 0u); }
bool koral_image_is_format_supported(const uint32_t f, const uint32_t usage)
{
    return Guarded([&] { return Image::IsFormatSupported(static_cast<Image::Format>(f), FlagsOf<Image::Usage>(usage)); }, false);
}
bool koral_image_is_block_compressed(const uint32_t f) { return Image::IsBlockCompressed(static_cast<Image::Format>(f)); }
void koral_image_block_extent(const uint32_t f, uint32_t* x, uint32_t* y)
{
    const auto e = Image::BlockExtent(static_cast<Image::Format>(f));
    if (x) *x = e.x;
    if (y) *y = e.y;
}
uint32_t koral_image_block_size(const uint32_t f) { return Image::BlockSize(static_cast<Image::Format>(f)); }
uint64_t koral_image_size_of_region(const uint32_t f, const uint32_t x, const uint32_t y, const uint32_t z, const uint32_t layers)
{
    return Guarded([&] { return Image::SizeOfRegion(static_cast<Image::Format>(f), {x, y, z}, layers); }, glm::u64{0});
}
bool koral_is_depth_stencil_format(const uint32_t f) { return IsDepthStencilFormat(static_cast<Image::Format>(f)); }
bool koral_is_stencil_format(const uint32_t f) { return IsStencilFormat(static_cast<Image::Format>(f)); }

// ---- ImageView, Sampler, BufferView ----------------------------------------------------------------------------

KoralImageViewBuilder* koral_image_view_builder_new(KoralImage* image)
{
    return Guarded([&]() -> KoralBuilder* { return new TypedBuilder<ImageView::Builder>(RefOf<Image>(image)); },
                   static_cast<KoralBuilder*>(nullptr));
}
void koral_image_view_builder_set_view_type(KoralImageViewBuilder* b, const uint32_t v) { Set<ImageView::Builder>(b, [&](auto& x) { x.SetViewType(static_cast<ImageView::Type>(v)); }); }
void koral_image_view_builder_set_base_mip_level(KoralImageViewBuilder* b, const uint32_t v) { Set<ImageView::Builder>(b, [&](auto& x) { x.SetBaseMipLevel(v); }); }
void koral_image_view_builder_set_mip_level_count(KoralImageViewBuilder* b, const uint32_t v) { Set<ImageView::Builder>(b, [&](auto& x) { x.SetMipLevelCount(v); }); }
void koral_image_view_builder_set_base_array_layer(KoralImageViewBuilder* b, const uint32_t v) { Set<ImageView::Builder>(b, [&](auto& x) { x.SetBaseArrayLayer(v); }); }
void koral_image_view_builder_set_array_layer_count(KoralImageViewBuilder* b, const uint32_t v) { Set<ImageView::Builder>(b, [&](auto& x) { x.SetArrayLayerCount(v); }); }
void koral_image_view_builder_set_component_mapping(KoralImageViewBuilder* b, const uint32_t s[4])
{
    Set<ImageView::Builder>(b, [&](auto& x) {
        x.SetComponentMapping(ImageView::ComponentMapping{}
            .SetR(static_cast<ImageView::Swizzle>(s[0])).SetG(static_cast<ImageView::Swizzle>(s[1]))
            .SetB(static_cast<ImageView::Swizzle>(s[2])).SetA(static_cast<ImageView::Swizzle>(s[3])));
    });
}
KoralImageView* koral_image_view_builder_build(KoralImageViewBuilder* b) { return Build<ImageView::Builder>(b, [](auto& x) { return x.Build(); }); }
KoralImage* koral_image_view_source_image(KoralImageView* r)
{
    return Guarded([&] { return Borrow(Get<ImageView>(r).SourceImage()); }, static_cast<KoralResource*>(nullptr));
}
uint32_t koral_image_view_view_type(KoralImageView* r) { return Guarded([&] { return static_cast<uint32_t>(Get<ImageView>(r).ViewType()); }, 0u); }
uint32_t koral_image_view_base_mip_level(KoralImageView* r) { return Guarded([&] { return Get<ImageView>(r).BaseMipLevel(); }, 0u); }
uint32_t koral_image_view_mip_level_count(KoralImageView* r) { return Guarded([&] { return Get<ImageView>(r).MipLevelCount(); }, 0u); }
uint32_t koral_image_view_base_array_layer(KoralImageView* r) { return Guarded([&] { return Get<ImageView>(r).BaseArrayLayer(); }, 0u); }
uint32_t koral_image_view_array_layer_count(KoralImageView* r) { return Guarded([&] { return Get<ImageView>(r).ArrayLayerCount(); }, 0u); }
void koral_image_view_components(KoralImageView* r, uint32_t s[4])
{
    GuardedVoid([&] {
        const auto c = Get<ImageView>(r).Components();
        s[0] = static_cast<uint32_t>(c.r); s[1] = static_cast<uint32_t>(c.g);
        s[2] = static_cast<uint32_t>(c.b); s[3] = static_cast<uint32_t>(c.a);
    });
}
bool koral_image_view_is_per_frame(KoralImageView* r) { return Guarded([&] { return Get<ImageView>(r).IsPerFrame(); }, false); }

KoralSamplerBuilder* koral_sampler_builder_new(void) { return new TypedBuilder<Sampler::Builder>(); }
void koral_sampler_builder_set_min_filter(KoralSamplerBuilder* b, const uint32_t v) { Set<Sampler::Builder>(b, [&](auto& x) { x.SetMinFilter(static_cast<Filter>(v)); }); }
void koral_sampler_builder_set_mag_filter(KoralSamplerBuilder* b, const uint32_t v) { Set<Sampler::Builder>(b, [&](auto& x) { x.SetMagFilter(static_cast<Filter>(v)); }); }
void koral_sampler_builder_set_mipmap_mode(KoralSamplerBuilder* b, const uint32_t v) { Set<Sampler::Builder>(b, [&](auto& x) { x.SetMipmapMode(static_cast<Sampler::MipmapMode>(v)); }); }
void koral_sampler_builder_set_address_mode_u(KoralSamplerBuilder* b, const uint32_t v) { Set<Sampler::Builder>(b, [&](auto& x) { x.SetAddressModeU(static_cast<Sampler::AddressMode>(v)); }); }
void koral_sampler_builder_set_address_mode_v(KoralSamplerBuilder* b, const uint32_t v) { Set<Sampler::Builder>(b, [&](auto& x) { x.SetAddressModeV(static_cast<Sampler::AddressMode>(v)); }); }
void koral_sampler_builder_set_address_mode_w(KoralSamplerBuilder* b, const uint32_t v) { Set<Sampler::Builder>(b, [&](auto& x) { x.SetAddressModeW(static_cast<Sampler::AddressMode>(v)); }); }
void koral_sampler_builder_set_mip_lod_bias(KoralSamplerBuilder* b, const float v) { Set<Sampler::Builder>(b, [&](auto& x) { x.SetMipLodBias(v); }); }
void koral_sampler_builder_set_anisotropy_enable(KoralSamplerBuilder* b, const bool v) { Set<Sampler::Builder>(b, [&](auto& x) { x.SetAnisotropyEnable(v); }); }
void koral_sampler_builder_set_max_anisotropy(KoralSamplerBuilder* b, const float v) { Set<Sampler::Builder>(b, [&](auto& x) { x.SetMaxAnisotropy(v); }); }
void koral_sampler_builder_set_compare_enable(KoralSamplerBuilder* b, const bool v) { Set<Sampler::Builder>(b, [&](auto& x) { x.SetCompareEnable(v); }); }
void koral_sampler_builder_set_compare_op(KoralSamplerBuilder* b, const uint32_t v) { Set<Sampler::Builder>(b, [&](auto& x) { x.SetCompareOp(static_cast<CompareOp>(v)); }); }
void koral_sampler_builder_set_min_lod(KoralSamplerBuilder* b, const float v) { Set<Sampler::Builder>(b, [&](auto& x) { x.SetMinLod(v); }); }
void koral_sampler_builder_set_max_lod(KoralSamplerBuilder* b, const float v) { Set<Sampler::Builder>(b, [&](auto& x) { x.SetMaxLod(v); }); }
void koral_sampler_builder_set_unnormalized_coordinates(KoralSamplerBuilder* b, const bool v) { Set<Sampler::Builder>(b, [&](auto& x) { x.SetUnnormalizedCoordinates(v); }); }
KoralSampler* koral_sampler_builder_build(KoralSamplerBuilder* b) { return Build<Sampler::Builder>(b, [](auto& x) { return x.Build(); }); }

KoralBufferViewBuilder* koral_buffer_view_builder_new(KoralBuffer* buffer)
{
    return Guarded([&]() -> KoralBuilder* { return new TypedBuilder<BufferView::Builder>(RefOf<Buffer>(buffer)); },
                   static_cast<KoralBuilder*>(nullptr));
}
void koral_buffer_view_builder_set_format(KoralBufferViewBuilder* b, const uint32_t v) { Set<BufferView::Builder>(b, [&](auto& x) { x.SetFormat(static_cast<Image::Format>(v)); }); }
void koral_buffer_view_builder_set_offset(KoralBufferViewBuilder* b, const int64_t v) { Set<BufferView::Builder>(b, [&](auto& x) { x.SetOffset(v); }); }
void koral_buffer_view_builder_set_range(KoralBufferViewBuilder* b, const int64_t v) { Set<BufferView::Builder>(b, [&](auto& x) { x.SetRange(v); }); }
KoralBufferView* koral_buffer_view_builder_build(KoralBufferViewBuilder* b) { return Build<BufferView::Builder>(b, [](auto& x) { return x.Build(); }); }
KoralBuffer* koral_buffer_view_source_buffer(KoralBufferView* r)
{
    return Guarded([&] { return Borrow(Get<BufferView>(r).SourceBuffer()); }, static_cast<KoralResource*>(nullptr));
}
uint32_t koral_buffer_view_pixel_format(KoralBufferView* r) { return Guarded([&] { return static_cast<uint32_t>(Get<BufferView>(r).PixelFormat()); }, 0u); }
int64_t koral_buffer_view_offset(KoralBufferView* r) { return Guarded([&] { return Get<BufferView>(r).Offset(); }, glm::i64{0}); }
int64_t koral_buffer_view_range(KoralBufferView* r) { return Guarded([&] { return Get<BufferView>(r).Range(); }, glm::i64{0}); }
uint64_t koral_buffer_view_texel_count(KoralBufferView* r) { return Guarded([&] { return Get<BufferView>(r).TexelCount(); }, glm::u64{0}); }

// ---- Shader ------------------------------------------------------------------------------------------------------

KoralShaderBuilder* koral_shader_builder_new(void) { return new TypedBuilder<Shader::Builder>(); }
void koral_shader_builder_set_stage(KoralShaderBuilder* b, const uint32_t v) { Set<Shader::Builder>(b, [&](auto& x) { x.SetStage(static_cast<Shader::Stage>(v)); }); }
void koral_shader_builder_set_path(KoralShaderBuilder* b, const char* path) { Set<Shader::Builder>(b, [&](auto& x) { x.SetPath(path ? path : ""); }); }
void koral_shader_builder_set_entry_point(KoralShaderBuilder* b, const char* module, const char* entry)
{
    Set<Shader::Builder>(b, [&](auto& x) {
        if (module) x.SetEntryPoint(module, entry ? entry : "");
        else x.SetEntryPoint(entry ? entry : "");
    });
}
void koral_shader_builder_set_lang(KoralShaderBuilder* b, const uint32_t v) { Set<Shader::Builder>(b, [&](auto& x) { x.SetLang(static_cast<Shader::Lang>(v)); }); }
KoralShader* koral_shader_builder_build(KoralShaderBuilder* b) { return Build<Shader::Builder>(b, [](auto& x) { return x.Build(); }); }
KoralShader* koral_shader_builder_get_or_build(KoralShaderBuilder* b, const char* identifier)
{
    return Guarded([&] {
        return Borrow(BuilderOf<Shader::Builder>(b).GetOrBuild(identifier ? identifier : ""));
    }, static_cast<KoralResource*>(nullptr));
}
uint32_t koral_shader_shader_stage(KoralShader* r) { return Guarded([&] { return static_cast<uint32_t>(Get<Shader>(r).ShaderStage()); }, 0u); }
uint32_t koral_shader_language(KoralShader* r) { return Guarded([&] { return static_cast<uint32_t>(Get<Shader>(r).Language()); }, 0u); }
const char* koral_shader_source_path(KoralShader* r) { return Guarded([&] { return Keep(Get<Shader>(r).SourcePath().string()); }, Keep("")); }
void koral_shader_add_search_path(const char* directory, const bool front)
{
    GuardedVoid([&] { Shader::AddSearchPath(directory ? directory : "", front); });
}

// ---- pipelines -------------------------------------------------------------------------------------------------------

} // extern "C"

namespace
{
    InputAssemblyState InputAssemblyOf(const KoralInputAssemblyState& s)
    {
        return {.topology = static_cast<Topology>(s.topology), .primitiveRestartEnable = s.primitive_restart_enable};
    }

    RasterizationState RasterizationOf(const KoralRasterizationState& s)
    {
        return {
            .depthClampEnable = s.depth_clamp_enable,
            .rasterizerDiscardEnable = s.rasterizer_discard_enable,
            .polygonMode = static_cast<PolygonMode>(s.polygon_mode),
            .cullMode = FlagsOf<CullMode>(s.cull_mode),
            .frontFace = static_cast<FrontFace>(s.front_face),
            .depthBiasEnable = s.depth_bias_enable,
            .depthBiasConstantFactor = s.depth_bias_constant_factor,
            .depthBiasClamp = s.depth_bias_clamp,
            .depthBiasSlopeFactor = s.depth_bias_slope_factor,
            .lineWidth = s.line_width,
        };
    }

    StencilOpState StencilOf(const KoralStencilOpState& s)
    {
        return {
            .failOp = static_cast<StencilOp>(s.fail_op), .passOp = static_cast<StencilOp>(s.pass_op),
            .depthFailOp = static_cast<StencilOp>(s.depth_fail_op), .compareOp = static_cast<CompareOp>(s.compare_op),
            .compareMask = s.compare_mask, .writeMask = s.write_mask, .reference = s.reference,
        };
    }

    DepthStencilState DepthStencilOf(const KoralDepthStencilState& s)
    {
        return {
            .depthTestEnable = s.depth_test_enable,
            .depthWriteEnable = s.depth_write_enable,
            .depthCompareOp = static_cast<CompareOp>(s.depth_compare_op),
            .depthBoundsEnable = s.depth_bounds_enable,
            .stencilEnable = s.stencil_enable,
            .stencilFront = StencilOf(s.stencil_front),
            .stencilBack = StencilOf(s.stencil_back),
            .minDepth = s.min_depth,
            .maxDepth = s.max_depth,
        };
    }

    ColorBlendState ColorBlendOf(const KoralColorBlendState& s)
    {
        ColorBlendState out;
        out.enableLogicOp = s.enable_logic_op;
        out.logicOp = static_cast<LogicOp>(s.logic_op);
        for (std::size_t i = 0; i < s.attachment_count; ++i) {
            const auto& a = s.attachments[i];
            out.attachments.push_back({
                .blendEnable = a.blend_enable,
                .srcColorBlendFactor = static_cast<BlendFactor>(a.src_color_blend_factor),
                .dstColorBlendFactor = static_cast<BlendFactor>(a.dst_color_blend_factor),
                .colorBlendOp = static_cast<BlendOp>(a.color_blend_op),
                .srcAlphaBlendFactor = static_cast<BlendFactor>(a.src_alpha_blend_factor),
                .dstAlphaBlendFactor = static_cast<BlendFactor>(a.dst_alpha_blend_factor),
                .alphaBlendOp = static_cast<BlendOp>(a.alpha_blend_op),
                .colorWriteMask = FlagsOf<ColorComponent>(a.color_write_mask),
            });
        }
        for (int i = 0; i < 4; ++i) out.blendConstants[i] = s.blend_constants[i];
        return out;
    }

    template<typename B>
    void SpecializationConstant(B& builder, const uint32_t id, const void* value, const uint32_t bytes)
    {
        if (!value) throw std::runtime_error("a specialization constant needs a value");
        if (bytes == 4) { std::uint32_t v; std::memcpy(&v, value, 4); builder.SetSpecializationConstant(id, v); }
        else if (bytes == 8) { std::uint64_t v; std::memcpy(&v, value, 8); builder.SetSpecializationConstant(id, v); }
        else throw std::runtime_error("a specialization constant is 4 or 8 bytes");
    }
}

extern "C" {

KoralGraphicsPipelineBuilder* koral_graphics_pipeline_builder_new(void) { return new TypedBuilder<GraphicsPipeline::Builder>(); }
void koral_graphics_pipeline_builder_set_vertex_shader(KoralGraphicsPipelineBuilder* b, KoralShader* shader, const KoralVertexLayout* layout)
{
    Set<GraphicsPipeline::Builder>(b, [&](auto& x) {
        if (layout) x.SetVertexShader(RefOf<Shader>(shader), VertexLayoutOf(*layout));
        else x.SetVertexShader(RefOf<Shader>(shader));
    });
}
void koral_graphics_pipeline_builder_set_tessellation_state(KoralGraphicsPipelineBuilder* b, KoralShader* control, KoralShader* evaluation,
                                                             const uint32_t points)
{
    Set<GraphicsPipeline::Builder>(b, [&](auto& x) {
        x.SetTessellationState(TessellationState{RefOf<Shader>(control), RefOf<Shader>(evaluation), points});
    });
}
void koral_graphics_pipeline_builder_set_geometry_shader(KoralGraphicsPipelineBuilder* b, KoralShader* s) { Set<GraphicsPipeline::Builder>(b, [&](auto& x) { x.SetGeometryShader(RefOf<Shader>(s)); }); }
void koral_graphics_pipeline_builder_set_fragment_shader(KoralGraphicsPipelineBuilder* b, KoralShader* s) { Set<GraphicsPipeline::Builder>(b, [&](auto& x) { x.SetFragmentShader(RefOf<Shader>(s)); }); }
void koral_graphics_pipeline_builder_set_task_shader(KoralGraphicsPipelineBuilder* b, KoralShader* s) { Set<GraphicsPipeline::Builder>(b, [&](auto& x) { x.SetTaskShader(RefOf<Shader>(s)); }); }
void koral_graphics_pipeline_builder_set_mesh_shader(KoralGraphicsPipelineBuilder* b, KoralShader* s) { Set<GraphicsPipeline::Builder>(b, [&](auto& x) { x.SetMeshShader(RefOf<Shader>(s)); }); }
void koral_graphics_pipeline_builder_set_input_assembly_state(KoralGraphicsPipelineBuilder* b, const KoralInputAssemblyState* s)
{
    Set<GraphicsPipeline::Builder>(b, [&](auto& x) { x.SetInputAssemblyState(InputAssemblyOf(*s)); });
}
void koral_graphics_pipeline_builder_set_rasterization_state(KoralGraphicsPipelineBuilder* b, const KoralRasterizationState* s)
{
    Set<GraphicsPipeline::Builder>(b, [&](auto& x) { x.SetRasterizationState(RasterizationOf(*s)); });
}
void koral_graphics_pipeline_builder_set_multisample_state(KoralGraphicsPipelineBuilder* b, const KoralMultisampleState* s)
{
    Set<GraphicsPipeline::Builder>(b, [&](auto& x) {
        x.SetMultisampleState({static_cast<SampleCount>(s->sample_count), s->sample_shading_enable, s->min_sample_shading});
    });
}
void koral_graphics_pipeline_builder_set_depth_stencil_state(KoralGraphicsPipelineBuilder* b, const KoralDepthStencilState* s)
{
    Set<GraphicsPipeline::Builder>(b, [&](auto& x) { x.SetDepthStencilState(DepthStencilOf(*s)); });
}
void koral_graphics_pipeline_builder_set_color_blend_state(KoralGraphicsPipelineBuilder* b, const KoralColorBlendState* s)
{
    Set<GraphicsPipeline::Builder>(b, [&](auto& x) { x.SetColorBlendState(ColorBlendOf(*s)); });
}
void koral_graphics_pipeline_builder_set_framebuffer(KoralGraphicsPipelineBuilder* b, KoralFramebuffer* f)
{
    Set<GraphicsPipeline::Builder>(b, [&](auto& x) { x.SetFramebuffer(RefOf<Framebuffer>(f)); });
}
void koral_graphics_pipeline_builder_set_specialization_constant(KoralGraphicsPipelineBuilder* b, const uint32_t id, const void* v, const uint32_t n)
{
    Set<GraphicsPipeline::Builder>(b, [&](auto& x) { SpecializationConstant(x, id, v, n); });
}
KoralGraphicsPipeline* koral_graphics_pipeline_builder_build(KoralGraphicsPipelineBuilder* b)
{
    return Build<GraphicsPipeline::Builder>(b, [](auto& x) { return x.Build(); });
}

KoralComputePipelineBuilder* koral_compute_pipeline_builder_new(void) { return new TypedBuilder<ComputePipeline::Builder>(); }
void koral_compute_pipeline_builder_set_compute_shader(KoralComputePipelineBuilder* b, KoralShader* s)
{
    Set<ComputePipeline::Builder>(b, [&](auto& x) { x.SetComputeShader(RefOf<Shader>(s)); });
}
void koral_compute_pipeline_builder_set_specialization_constant(KoralComputePipelineBuilder* b, const uint32_t id, const void* v, const uint32_t n)
{
    Set<ComputePipeline::Builder>(b, [&](auto& x) { SpecializationConstant(x, id, v, n); });
}
KoralComputePipeline* koral_compute_pipeline_builder_build(KoralComputePipelineBuilder* b)
{
    return Build<ComputePipeline::Builder>(b, [](auto& x) { return x.Build(); });
}

KoralRayTracingPipelineBuilder* koral_ray_tracing_pipeline_builder_new(void) { return new TypedBuilder<RayTracingPipeline::Builder>(); }
void koral_ray_tracing_pipeline_builder_set_raygen_shader(KoralRayTracingPipelineBuilder* b, KoralShader* s)
{
    Set<RayTracingPipeline::Builder>(b, [&](auto& x) { x.SetRaygenShader(RefOf<Shader>(s)); });
}
void koral_ray_tracing_pipeline_builder_add_miss_shader(KoralRayTracingPipelineBuilder* b, KoralShader* s)
{
    Set<RayTracingPipeline::Builder>(b, [&](auto& x) { x.AddMissShader(RefOf<Shader>(s)); });
}
void koral_ray_tracing_pipeline_builder_add_hit_group(KoralRayTracingPipelineBuilder* b, KoralShader* closest, KoralShader* any,
                                                       KoralShader* intersection)
{
    Set<RayTracingPipeline::Builder>(b, [&](auto& x) {
        RayTracingPipeline::HitGroup group;
        if (closest) group.closestHitShader = RefOf<Shader>(closest);
        if (any) group.anyHitShader = RefOf<Shader>(any);
        if (intersection) group.intersectionShader = RefOf<Shader>(intersection);
        x.AddHitGroup(group);
    });
}
void koral_ray_tracing_pipeline_builder_add_callable_shader(KoralRayTracingPipelineBuilder* b, KoralShader* s)
{
    Set<RayTracingPipeline::Builder>(b, [&](auto& x) { x.AddCallableShader(RefOf<Shader>(s)); });
}
void koral_ray_tracing_pipeline_builder_set_max_recursion_depth(KoralRayTracingPipelineBuilder* b, const uint32_t d)
{
    Set<RayTracingPipeline::Builder>(b, [&](auto& x) { x.SetMaxRecursionDepth(d); });
}
KoralRayTracingPipeline* koral_ray_tracing_pipeline_builder_build(KoralRayTracingPipelineBuilder* b)
{
    return Build<RayTracingPipeline::Builder>(b, [](auto& x) { return x.Build(); });
}
uint32_t koral_ray_tracing_pipeline_max_recursion_depth(KoralRayTracingPipeline* r)
{
    return Guarded([&] { return Get<RayTracingPipeline>(r).MaxRecursionDepth(); }, 0u);
}

KoralDescriptorSetLayout* koral_pipeline_set_layout(KoralResource* pipeline, const uint32_t index)
{
    return Guarded([&] {
        const auto ref = PipelineOf(pipeline);
        if (!ref.Get()) ThrowUnusable(*pipeline);
        return Borrow(ref->SetLayoutRef(index));
    }, static_cast<KoralResource*>(nullptr));
}
bool koral_pipeline_uses_device_addresses(KoralResource* pipeline)
{
    return Guarded([&] { const auto ref = PipelineOf(pipeline); return ref.Get() && ref->UsesDeviceAddresses(); }, false);
}
bool koral_pipeline_has_push_constant(KoralResource* pipeline, const char* name)
{
    return Guarded([&] { const auto ref = PipelineOf(pipeline); return ref.Get() && ref->FindPushConstant(name ? name : "") != nullptr; }, false);
}
int64_t koral_descriptor_set_layout_find_binding(KoralDescriptorSetLayout* layout, const char* name)
{
    return Guarded([&]() -> int64_t {
        const auto binding = Get<DescriptorSetLayout>(layout).FindBinding(name ? name : "");
        return binding ? static_cast<int64_t>(*binding) : -1;
    }, int64_t{-1});
}

// ---- DescriptorSet ----------------------------------------------------------------------------------------------------

KoralDescriptorSetBuilder* koral_descriptor_set_builder_new(KoralResource* pipelineOrLayout, const uint32_t set)
{
    return Guarded([&]() -> KoralBuilder* {
        if (pipelineOrLayout && pipelineOrLayout->Kind() == KORAL_RESOURCE_DESCRIPTOR_SET_LAYOUT)
            return new TypedBuilder<DescriptorSet::Builder>(RefOf<DescriptorSetLayout>(pipelineOrLayout));
        return new TypedBuilder<DescriptorSet::Builder>(PipelineOf(pipelineOrLayout), set);
    }, static_cast<KoralBuilder*>(nullptr));
}

} // extern "C"

namespace
{
    // One Write, at a name or a binding, as the C++ overloads have it.
    template<typename... Args>
    void WriteAt(KoralBuilder* b, const char* name, const uint32_t binding, const uint32_t index, Args&&... args)
    {
        Set<DescriptorSet::Builder>(b, [&](auto& x) {
            if (name) x.Write(std::string_view(name), args...);
            else x.Write(binding, args..., index);
        });
    }

    template<typename... Args>
    KoralStatus RebindAt(KoralResource* set, const char* name, const uint32_t binding, const uint32_t index, Args&&... args)
    {
        return Guarded([&] {
            auto& s = GetWritable<DescriptorSet>(set);
            if (name) s.Rebind(std::string_view(name), args...);
            else s.Rebind(binding, args..., index);
            return KORAL_OK;
        }, KORAL_ERROR);
    }
}

extern "C" {

void koral_descriptor_set_builder_write_buffer(KoralDescriptorSetBuilder* b, const char* name, const uint32_t binding, const uint32_t index, KoralBuffer* buffer)
{
    GuardedVoid([&] { WriteAt(b, name, binding, index, RefOf<Buffer>(buffer)); });
}
void koral_descriptor_set_builder_write_buffer_slice(KoralDescriptorSetBuilder* b, const char* name, const uint32_t binding, const uint32_t index,
                                                      KoralBuffer* buffer, const int64_t offset, const int64_t size)
{
    GuardedVoid([&] { WriteAt(b, name, binding, index, Buffer::SliceOf(RefOf<Buffer>(buffer), offset, size)); });
}
void koral_descriptor_set_builder_write_buffer_view(KoralDescriptorSetBuilder* b, const char* name, const uint32_t binding, const uint32_t index,
                                                     KoralBufferView* view)
{
    GuardedVoid([&] { WriteAt(b, name, binding, index, RefOf<BufferView>(view)); });
}
void koral_descriptor_set_builder_write_image(KoralDescriptorSetBuilder* b, const char* name, const uint32_t binding, const uint32_t index,
                                               KoralImage* image, KoralSampler* sampler)
{
    GuardedVoid([&] {
        if (sampler) WriteAt(b, name, binding, index, RefOf<Image>(image), RefOf<Sampler>(sampler));
        else WriteAt(b, name, binding, index, RefOf<Image>(image));
    });
}
void koral_descriptor_set_builder_write_image_view(KoralDescriptorSetBuilder* b, const char* name, const uint32_t binding, const uint32_t index,
                                                    KoralImageView* view, KoralSampler* sampler)
{
    GuardedVoid([&] {
        if (sampler) WriteAt(b, name, binding, index, RefOf<ImageView>(view), RefOf<Sampler>(sampler));
        else WriteAt(b, name, binding, index, RefOf<ImageView>(view));
    });
}
void koral_descriptor_set_builder_write_sampler(KoralDescriptorSetBuilder* b, const char* name, const uint32_t binding, const uint32_t index,
                                                 KoralSampler* sampler)
{
    GuardedVoid([&] { WriteAt(b, name, binding, index, RefOf<Sampler>(sampler)); });
}
void koral_descriptor_set_builder_write_acceleration_structure(KoralDescriptorSetBuilder* b, const char* name, const uint32_t binding,
                                                                const uint32_t index, KoralAccelerationStructure* structure)
{
    GuardedVoid([&] { WriteAt(b, name, binding, index, RefOf<AccelerationStructure>(structure)); });
}
KoralDescriptorSet* koral_descriptor_set_builder_build(KoralDescriptorSetBuilder* b)
{
    return Build<DescriptorSet::Builder>(b, [](auto& x) { return x.Build(); });
}

KoralStatus koral_descriptor_set_rebind_buffer(KoralDescriptorSet* s, const char* name, const uint32_t binding, const uint32_t index, KoralBuffer* buffer)
{
    return Guarded([&] { return RebindAt(s, name, binding, index, RefOf<Buffer>(buffer)); }, KORAL_ERROR);
}
KoralStatus koral_descriptor_set_rebind_buffer_slice(KoralDescriptorSet* s, const char* name, const uint32_t binding, const uint32_t index,
                                                     KoralBuffer* buffer, const int64_t offset, const int64_t size)
{
    return Guarded([&] { return RebindAt(s, name, binding, index, Buffer::SliceOf(RefOf<Buffer>(buffer), offset, size)); }, KORAL_ERROR);
}
KoralStatus koral_descriptor_set_rebind_buffer_view(KoralDescriptorSet* s, const char* name, const uint32_t binding, const uint32_t index,
                                                    KoralBufferView* view)
{
    return Guarded([&] { return RebindAt(s, name, binding, index, RefOf<BufferView>(view)); }, KORAL_ERROR);
}
KoralStatus koral_descriptor_set_rebind_image_view(KoralDescriptorSet* s, const char* name, const uint32_t binding, const uint32_t index,
                                                   KoralImageView* view, KoralSampler* sampler)
{
    return Guarded([&] {
        return sampler ? RebindAt(s, name, binding, index, RefOf<ImageView>(view), RefOf<Sampler>(sampler))
                       : RebindAt(s, name, binding, index, RefOf<ImageView>(view));
    }, KORAL_ERROR);
}
KoralStatus koral_descriptor_set_rebind_sampler(KoralDescriptorSet* s, const char* name, const uint32_t binding, const uint32_t index,
                                                KoralSampler* sampler)
{
    return Guarded([&] { return RebindAt(s, name, binding, index, RefOf<Sampler>(sampler)); }, KORAL_ERROR);
}
KoralStatus koral_descriptor_set_rebind_acceleration_structure(KoralDescriptorSet* s, const char* name, const uint32_t binding,
                                                               const uint32_t index, KoralAccelerationStructure* structure)
{
    return Guarded([&] { return RebindAt(s, name, binding, index, RefOf<AccelerationStructure>(structure)); }, KORAL_ERROR);
}
KoralDescriptorSetLayout* koral_descriptor_set_layout(KoralDescriptorSet* s)
{
    return Guarded([&] { return Borrow(Get<DescriptorSet>(s).Layout()); }, static_cast<KoralResource*>(nullptr));
}

// ---- Framebuffer --------------------------------------------------------------------------------------------------

} // extern "C"

namespace
{
    Framebuffer::Builder::AttachmentSource SourceOf(KoralResource* resource)
    {
        if (!resource) return {};
        if (resource->Kind() == KORAL_RESOURCE_IMAGE) return {RefOf<Image>(resource)};
        return {RefOf<ImageView>(resource)};
    }
}

extern "C" {

KoralFramebufferBuilder* koral_framebuffer_builder_new(void) { return new TypedBuilder<Framebuffer::Builder>(); }
void koral_framebuffer_builder_add_color(KoralFramebufferBuilder* b, const char* name, KoralResource* view, KoralResource* resolve,
                                         const KoralClearColor* clear)
{
    Set<Framebuffer::Builder>(b, [&](auto& x) {
        Framebuffer::Builder::ColorAttachment attachment{.name = name ? name : "", .view = SourceOf(view), .resolve = SourceOf(resolve)};
        if (clear && clear->components) attachment.clear = ClearColorOf(*clear);
        x.AddColor(attachment);
    });
}
void koral_framebuffer_builder_set_depth_stencil(KoralFramebufferBuilder* b, const uint32_t which, const char* name, KoralResource* view,
                                                 KoralResource* resolve, const float depth, const int32_t stencil)
{
    Set<Framebuffer::Builder>(b, [&](auto& x) {
        const Framebuffer::Builder::DepthStencilAttachment attachment{
            .name = name ? name : "", .view = SourceOf(view), .resolve = SourceOf(resolve), .depth = depth, .stencil = stencil};
        if (which == 0) x.SetDepth(attachment);
        else if (which == 1) x.SetStencil(attachment);
        else x.SetDepthStencil(attachment);
    });
}
void koral_framebuffer_builder_set_resolve_mode(KoralFramebufferBuilder* b, const uint32_t mode)
{
    Set<Framebuffer::Builder>(b, [&](auto& x) { x.SetResolveMode(static_cast<ResolveMode>(mode)); });
}
KoralFramebuffer* koral_framebuffer_builder_build(KoralFramebufferBuilder* b) { return Build<Framebuffer::Builder>(b, [](auto& x) { return x.Build(); }); }

bool koral_framebuffer_is_default(KoralFramebuffer* r) { return Guarded([&] { return Get<Framebuffer>(r).IsDefault(); }, false); }
uint32_t koral_framebuffer_color_attachment_count(KoralFramebuffer* r) { return Guarded([&] { return Get<Framebuffer>(r).ColorAttachmentCount(); }, 0u); }
uint32_t koral_framebuffer_samples(KoralFramebuffer* r) { return Guarded([&] { return static_cast<uint32_t>(Get<Framebuffer>(r).Samples()); }, 0u); }
void koral_framebuffer_extent(KoralFramebuffer* r, uint32_t* x, uint32_t* y)
{
    const auto e = Guarded([&] { return Get<Framebuffer>(r).Extent(); }, glm::uvec2(0));
    if (x) *x = e.x;
    if (y) *y = e.y;
}

} // extern "C"

namespace
{
    template<typename Body>
    KoralResource* BorrowFrom(Body&& body) { return Guarded([&] { return Borrow(body()); }, static_cast<KoralResource*>(nullptr)); }
}

extern "C" {

KoralImageView* koral_framebuffer_color_attachment(KoralFramebuffer* r, const uint32_t i) { return BorrowFrom([&] { return Get<Framebuffer>(r).ColorAttachment(i); }); }
bool koral_framebuffer_has_depth_attachment(KoralFramebuffer* r) { return Guarded([&] { return Get<Framebuffer>(r).HasDepthAttachment(); }, false); }
KoralImageView* koral_framebuffer_depth_attachment(KoralFramebuffer* r) { return BorrowFrom([&] { return Get<Framebuffer>(r).DepthAttachment(); }); }
bool koral_framebuffer_has_stencil_attachment(KoralFramebuffer* r) { return Guarded([&] { return Get<Framebuffer>(r).HasStencilAttachment(); }, false); }
KoralImageView* koral_framebuffer_stencil_attachment(KoralFramebuffer* r) { return BorrowFrom([&] { return Get<Framebuffer>(r).StencilAttachment(); }); }
bool koral_framebuffer_has_resolve_attachments(KoralFramebuffer* r) { return Guarded([&] { return Get<Framebuffer>(r).HasResolveAttachments(); }, false); }
KoralImageView* koral_framebuffer_resolve_attachment(KoralFramebuffer* r, const uint32_t i) { return BorrowFrom([&] { return Get<Framebuffer>(r).ResolveAttachment(i); }); }
KoralImageView* koral_framebuffer_attachment_named(KoralFramebuffer* r, const char* n) { return BorrowFrom([&] { return Get<Framebuffer>(r).AttachmentNamed(n ? n : ""); }); }
KoralImage* koral_framebuffer_image_named(KoralFramebuffer* r, const char* n) { return BorrowFrom([&] { return Get<Framebuffer>(r).ImageNamed(n ? n : ""); }); }
KoralImage* koral_framebuffer_color_image(KoralFramebuffer* r, const uint32_t i) { return BorrowFrom([&] { return Get<Framebuffer>(r).ColorImage(i); }); }
KoralImage* koral_framebuffer_depth_image(KoralFramebuffer* r) { return BorrowFrom([&] { return Get<Framebuffer>(r).DepthImage(); }); }
uint32_t koral_framebuffer_attachment_name_count(KoralFramebuffer* r)
{
    return Guarded([&] { return static_cast<uint32_t>(Get<Framebuffer>(r).AttachmentNames().size()); }, 0u);
}
const char* koral_framebuffer_attachment_name(KoralFramebuffer* r, const uint32_t i)
{
    return Guarded([&] { return Keep(Get<Framebuffer>(r).AttachmentNames().at(i)); }, Keep(""));
}
void koral_framebuffer_clear_color_at(KoralFramebuffer* r, const uint32_t i, KoralClearColor* color)
{
    GuardedVoid([&] { *color = ClearColorOf(Get<Framebuffer>(r).ClearColorAt(i)); });
}
float koral_framebuffer_clear_depth(KoralFramebuffer* r) { return Guarded([&] { return Get<Framebuffer>(r).ClearDepth(); }, 1.f); }
int32_t koral_framebuffer_clear_stencil(KoralFramebuffer* r) { return Guarded([&] { return Get<Framebuffer>(r).ClearStencil(); }, 0); }
uint32_t koral_framebuffer_resolve_method(KoralFramebuffer* r) { return Guarded([&] { return static_cast<uint32_t>(Get<Framebuffer>(r).ResolveMethod()); }, 0u); }
void koral_framebuffer_resize(KoralFramebuffer* r, const uint32_t x, const uint32_t y)
{
    GuardedVoid([&] { GetWritable<Framebuffer>(r).Resize({x, y}); });
}

// ---- Mesh, AccelerationStructure ------------------------------------------------------------------------------------

KoralMeshBuilder* koral_mesh_builder_new(void) { return new TypedBuilder<Mesh::Builder>(); }
void koral_mesh_builder_set_vertex_buffer(KoralMeshBuilder* b, const uint32_t binding, KoralBuffer* buffer)
{
    Set<Mesh::Builder>(b, [&](auto& x) {
        auto& handle = HandleOf<Buffer>(buffer);
        if (!handle.writable.Alive() && !handle.writable.Get()) throw std::runtime_error("a mesh's buffers are its to change: pass one you own");
        x.SetVertexBuffer(binding, handle.writable);
    });
}
void koral_mesh_builder_set_index_buffer(KoralMeshBuilder* b, KoralBuffer* buffer, const uint32_t type)
{
    Set<Mesh::Builder>(b, [&](auto& x) {
        auto& handle = HandleOf<Buffer>(buffer);
        if (!handle.writable.Alive() && !handle.writable.Get()) throw std::runtime_error("a mesh's buffers are its to change: pass one you own");
        x.SetIndexBuffer(handle.writable, static_cast<ChannelType>(type));
    });
}
void koral_mesh_builder_set_vertex_layout(KoralMeshBuilder* b, const KoralVertexLayout* layout)
{
    Set<Mesh::Builder>(b, [&](auto& x) { x.SetVertexLayout(VertexLayoutOf(*layout)); });
}
KoralMesh* koral_mesh_builder_build(KoralMeshBuilder* b) { return Build<Mesh::Builder>(b, [](auto& x) { return x.Build(); }); }
uint64_t koral_mesh_vertex_count(KoralMesh* r) { return Guarded([&] { return Get<Mesh>(r).VertexCount(); }, glm::u64{0}); }
bool koral_mesh_has_index_buffer(KoralMesh* r) { return Guarded([&] { return Get<Mesh>(r).HasIndexBuffer(); }, false); }
bool koral_mesh_index_count(KoralMesh* r, uint32_t* count)
{
    return Guarded([&] {
        const auto c = Get<Mesh>(r).IndexCount();
        if (c && count) *count = *c;
        return c.has_value();
    }, false);
}
bool koral_mesh_index_type(KoralMesh* r, uint32_t* type)
{
    return Guarded([&] {
        const auto t = Get<Mesh>(r).IndexType();
        if (t && type) *type = static_cast<uint32_t>(*t);
        return t.has_value();
    }, false);
}
uint32_t koral_mesh_vertex_buffer_count(KoralMesh* r) { return Guarded([&] { return static_cast<uint32_t>(Get<Mesh>(r).VertexBuffers().size()); }, 0u); }
KoralBuffer* koral_mesh_vertex_buffer(KoralMesh* r, const uint32_t i) { return BorrowFrom([&] { return Get<Mesh>(r).VertexBuffers().at(i); }); }
KoralBuffer* koral_mesh_index_buffer(KoralMesh* r)
{
    return Guarded([&]() -> KoralResource* {
        const auto buffer = Get<Mesh>(r).IndexBuffer();
        return buffer ? Borrow(*buffer) : nullptr;
    }, static_cast<KoralResource*>(nullptr));
}

KoralAccelerationStructureBuilder* koral_acceleration_structure_builder_new(void) { return new TypedBuilder<AccelerationStructure::Builder>(); }
void koral_acceleration_structure_builder_add_mesh(KoralAccelerationStructureBuilder* b, KoralMesh* mesh)
{
    Set<AccelerationStructure::Builder>(b, [&](auto& x) { x.AddMesh(RefOf<Mesh>(mesh)); });
}
void koral_acceleration_structure_builder_add_geometry(KoralAccelerationStructureBuilder* b, KoralMesh* mesh, const uint64_t firstVertex,
                                                       const uint64_t vertexCount, const uint64_t firstIndex, const uint64_t indexCount)
{
    Set<AccelerationStructure::Builder>(b, [&](auto& x) {
        x.AddGeometry({.mesh = RefOf<Mesh>(mesh), .firstVertex = firstVertex, .vertexCount = vertexCount,
                       .firstIndex = firstIndex, .indexCount = indexCount});
    });
}
void koral_acceleration_structure_builder_add_instance(KoralAccelerationStructureBuilder* b, KoralAccelerationStructure* blas,
                                                       const float transform[16], const uint32_t customIndex, const uint32_t hitGroup)
{
    Set<AccelerationStructure::Builder>(b, [&](auto& x) {
        AccelerationStructure::Instance instance{.blas = RefOf<AccelerationStructure>(blas), .instanceCustomIndex = customIndex,
                                                 .hitGroupIndex = hitGroup};
        if (transform) std::memcpy(&instance.transform[0][0], transform, sizeof(float) * 16);
        x.AddInstance(instance);
    });
}
KoralAccelerationStructure* koral_acceleration_structure_builder_build(KoralAccelerationStructureBuilder* b)
{
    return Build<AccelerationStructure::Builder>(b, [](auto& x) { return x.Build(); });
}
uint32_t koral_acceleration_structure_structure_type(KoralAccelerationStructure* r)
{
    return Guarded([&] { return static_cast<uint32_t>(Get<AccelerationStructure>(r).StructureType()); }, 0u);
}

// ---- Token -----------------------------------------------------------------------------------------------------------

KoralToken* koral_token_create(void) { return new KoralToken{Token::Create()}; }
KoralToken* koral_token_copy(KoralToken* token) { return token ? new KoralToken{token->token} : nullptr; }
void koral_token_destroy(KoralToken* token) { delete token; }
bool koral_token_ready(KoralToken* token) { return !token || token->token.Ready(); }
void koral_token_wait(KoralToken* token) { if (token) GuardedVoid([&] { token->token.Wait(); }); }
void koral_token_signal(KoralToken* token) { if (token) GuardedVoid([&] { token->token.Signal(); }); }
uint64_t koral_token_value(KoralToken* token) { return token ? token->token.Value() : 0; }

} // extern "C"

// ---- for modules' C interfaces (C++ linkage) --------------------------------------------------------------
kor::ResourceRef<const kor::Image> kor::capi::ImageOf(KoralResource* handle) { return RefOf<Image>(handle); }
KoralResource* kor::capi::BorrowImage(const ResourceRef<const Image>& image) { return Borrow(image); }
