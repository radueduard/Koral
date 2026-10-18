//
// kor::Bindless — what is checked before an index is handed out. The tables themselves are the
// backend's (src/backends/vulkan/bindlessTables.cpp), because under the descriptor heap an index is a
// heap slot, and the heap is the backend's.
//

#include <bindless.h>

#include <format>

#include <accelerationStructure.h>
#include <buffer.h>
#include <context.h>
#include <image.h>
#include <imageView.h>
#include <log.h>
#include <sampler.h>

#include "../backends/vulkan/bindlessTables.h"
#include "../backends/vulkan/vulkanContext.h"

namespace kor
{
    kor::u32 BindlessBinding(const BindlessKind kind)
    {
        // Fixed, because shaders spell them: koral_bindless.slang and koral_bindless.glsl declare these.
        switch (kind) {
        case BindlessKind::eSampledImage:          return 0;
        case BindlessKind::eStorageImage:          return 1;
        case BindlessKind::eStorageBuffer:         return 2;
        case BindlessKind::eUniformBuffer:         return 3;
        case BindlessKind::eSampler:               return 4;
        case BindlessKind::eAccelerationStructure: return 5;
        default:                                   return ~0u;
        }
    }

    const char* BindlessKindName(const BindlessKind kind)
    {
        switch (kind) {
        case BindlessKind::eSampledImage:          return "sampled image";
        case BindlessKind::eStorageImage:          return "storage image";
        case BindlessKind::eStorageBuffer:         return "storage buffer";
        case BindlessKind::eUniformBuffer:         return "uniform buffer";
        case BindlessKind::eSampler:               return "sampler";
        case BindlessKind::eAccelerationStructure: return "acceleration structure";
        default:                                   return "none";
        }
    }

    namespace
    {
        // Every refusal is said, and answered with the invalid index: a call that hands out an index is
        // not the place to throw from, and an index that is never valid shows up in the first frame.
        DescriptorIndex refuse(const BindlessKind kind, const std::string& why)
        {
            log::Error("[bindless] No {} index: {}", BindlessKindName(kind), why);
            return {};
        }

        bool deviceUp(const BindlessKind kind)
        {
            if (Context::HasDevice()) return true;
            refuse(kind, "there is no device yet. Ask for indices once the context is up.");
            return false;
        }

        DescriptorIndex image(const BindlessKind kind, const ResourceRef<const ImageView>& view,
                              const ResourceRef<const Image>& owner, const ImageShape shape)
        {
            if (!deviceUp(kind)) return {};
            if (!view.Valid()) return refuse(kind, "the image view is missing or failed to build.");
            if (view->IsPerFrame())
                return refuse(kind, "the image has a copy per frame in flight, and so no single index. Bind it through a descriptor set.");
            const auto source = view->SourceImage();
            if (!source.Valid()) return refuse(kind, "the view's image is missing or failed to build.");
            const auto usage = kind == BindlessKind::eStorageImage ? Image::Usage::eStorage : Image::Usage::eSampled;
            if (!(source->UsageFlags() & usage))
                return refuse(kind, std::format("the image was not created with Image::Usage::{}.",
                                                kind == BindlessKind::eStorageImage ? "eStorage" : "eSampled"));
            return vk::Context::Bindless().Acquire(kind, view, owner, shape);
        }

        DescriptorIndex buffer(const BindlessKind kind, const ResourceRef<const Buffer>& buffer)
        {
            if (!deviceUp(kind)) return {};
            if (!buffer.Valid()) return refuse(kind, "the buffer is missing or failed to build.");
            if (buffer->IsPerFrame())
                return refuse(kind, "the buffer has a copy per frame in flight, and so no single index. Bind it through a descriptor set.");
            const auto usage = kind == BindlessKind::eUniformBuffer ? Buffer::Usage::eUniform : Buffer::Usage::eStorage;
            if (!(buffer->UsageFlags() & usage))
                return refuse(kind, std::format("the buffer was not created with Buffer::Usage::{}.",
                                                kind == BindlessKind::eUniformBuffer ? "eUniform" : "eStorage"));
            return vk::Context::Bindless().Acquire(kind, buffer);
        }
    }

    DescriptorIndex Bindless::Sampled(const ResourceRef<const ImageView>& view)
    {
        return image(BindlessKind::eSampledImage, view, {}, ImageShape::eUnknown);
    }

    DescriptorIndex Bindless::Sampled(const ResourceRef<const Image>& texture, const ImageShape shape)
    {
        if (!texture.Valid()) return refuse(BindlessKind::eSampledImage, "the image is missing or failed to build.");
        return image(BindlessKind::eSampledImage, texture->View(shape), texture, shape);
    }

    DescriptorIndex Bindless::Storage(const ResourceRef<const ImageView>& view)
    {
        return image(BindlessKind::eStorageImage, view, {}, ImageShape::eUnknown);
    }

    DescriptorIndex Bindless::Storage(const ResourceRef<const Image>& texture, const ImageShape shape)
    {
        if (!texture.Valid()) return refuse(BindlessKind::eStorageImage, "the image is missing or failed to build.");
        return image(BindlessKind::eStorageImage, texture->View(shape), texture, shape);
    }

    DescriptorIndex Bindless::StorageBuffer(const ResourceRef<const Buffer>& storage)
    {
        return buffer(BindlessKind::eStorageBuffer, storage);
    }

    DescriptorIndex Bindless::UniformBuffer(const ResourceRef<const Buffer>& uniform)
    {
        return buffer(BindlessKind::eUniformBuffer, uniform);
    }

    DescriptorIndex Bindless::Sampler(const ResourceRef<const kor::Sampler>& sampler)
    {
        if (!deviceUp(BindlessKind::eSampler)) return {};
        if (!sampler.Valid()) return refuse(BindlessKind::eSampler, "the sampler is missing or failed to build.");
        return vk::Context::Bindless().Acquire(sampler);
    }

    DescriptorIndex Bindless::AccelerationStructure(const ResourceRef<const kor::AccelerationStructure>& structure)
    {
        constexpr auto kind = BindlessKind::eAccelerationStructure;
        if (!deviceUp(kind)) return {};
        if (!structure.Valid()) return refuse(kind, "the acceleration structure is missing or failed to build.");
        if (structure->StructureType() != kor::AccelerationStructure::Type::eTopLevel)
            return refuse(kind, "only a top-level structure is traced against; a bottom-level one is reached through it.");
        return vk::Context::Bindless().Acquire(structure);
    }

    void Bindless::Release(const DescriptorIndex handle)
    {
        if (!handle.Valid() || !Context::HasDevice()) return;
        vk::Context::Bindless().Release(handle);
    }

    kor::u32 Bindless::Capacity(const BindlessKind kind)
    {
        return Context::HasDevice() ? vk::Context::Bindless().Capacity(kind) : 0;
    }

    std::size_t Bindless::Count()
    {
        return Context::HasDevice() ? vk::Context::Bindless().Count() : 0;
    }

    std::optional<detail::BindlessTarget> detail::BindlessResource(const DescriptorIndex handle)
    {
        if (!handle.Valid() || !Context::HasDevice()) return std::nullopt;
        return vk::Context::Bindless().Target(handle);
    }

    void detail::CollectBindless()
    {
        if (Context::HasDevice()) vk::Context::Bindless().Collect();
    }
}
