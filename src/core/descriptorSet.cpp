//
// Created by radue on 3/4/2026.
//

#include <descriptorSet.h>
#include <descriptor.h>
#include <descriptorSetLayout.h>
#include <window.h>
#include <framebuffer.h>
#include <surface.h>

#include "../backends/open_gl/descriptorSet.h"
#include "../backends/vulkan/descriptorSet.h"

#include <ranges>

#include "buffer.h"
#include "imageView.h"


namespace gfx
{
    Descriptor::Descriptor(const ResourceRef<const Buffer>& buffer, const glm::i64 offset, const glm::i64 range)
        : valid(true), _descriptor(BufferDescriptor{ buffer, offset, range })
    {
        if (!buffer) {
            gfx::log::error("Attempted to create a buffer descriptor with an invalid buffer!");
            throw;
        }

        if (offset < 0 || offset >= buffer->getSize()) {
            gfx::log::error("Attempted to create a buffer descriptor with an invalid offset! Buffer size: {}, offset: {}", buffer->getSize(), offset);
            throw;
        }

        if (range < 0) {
            gfx::log::error("Attempted to create a buffer descriptor with a negative range! Range: {}", range);
            throw;
        }

        if (offset + range > buffer->getSize()) {
            gfx::log::error("Attempted to create a buffer descriptor with an offset + range that exceeds the buffer size! Buffer size: {}, offset: {}, range: {}", buffer->getSize(), offset, range);
            throw;
        }

        if (range == 0) {
            std::visit([](auto& d) {
                using D = std::decay_t<decltype(d)>;
                if constexpr (std::is_same_v<D, BufferDescriptor>) {
                    if (d._range == 0) {
                        d._range = d._buffer->getSize() - d._offset;
                    }
                }
            }, _descriptor);
        }
    }

    Descriptor::Descriptor(const ResourceRef<const ImageView>&imageView, const ResourceRef<const Sampler>&sampler)
        : valid(true), _descriptor(CombinedImageSamplerDescriptor{ imageView, sampler })
    {
        if (!imageView) {
            gfx::log::error("Attempted to create a combined image sampler descriptor with an invalid image view!");
            throw;
        }
        if (!sampler) {
            gfx::log::error("Attempted to create a combined image sampler descriptor with an invalid sampler!");
            throw;
        }
    }

    Descriptor::Descriptor(const ResourceRef<const ImageView>&imageView)
        : valid(true), _descriptor(ImageDescriptor{ imageView })
    {
        if (!imageView) {
            gfx::log::error("Attempted to create an image descriptor with an invalid image view!");
            throw;
        }
    }

    Descriptor::Descriptor(const ResourceRef<const Sampler>&sampler)
        : valid(true), _descriptor(SamplerDescriptor{ sampler }) {
        if (!sampler) {
            gfx::log::error("Attempted to create a sampler descriptor with an invalid sampler!");
            throw;
        }
    }

    const Buffer & Descriptor::getBuffer() const {
        if (!valid) {
            gfx::log::error("Attempted to get buffer from an invalid descriptor!");
            throw;
        }
        if (!std::holds_alternative<BufferDescriptor>(_descriptor)) {
            gfx::log::error("Attempted to get buffer from a descriptor that does not hold a buffer!");
            throw;
        }
        return *std::get<BufferDescriptor>(_descriptor)._buffer;
    }

    glm::i64 Descriptor::getOffset() const {
        if (!valid) {
            gfx::log::error("Attempted to get offset from an invalid descriptor!");
            throw;
        }
        if (!std::holds_alternative<BufferDescriptor>(_descriptor)) {
            gfx::log::error("Attempted to get offset from a descriptor that does not hold a buffer!");
            throw;
        }
        return std::get<BufferDescriptor>(_descriptor)._offset;
    }

    glm::i64 Descriptor::getRange() const {
        if (!valid) {
            gfx::log::error("Attempted to get range from an invalid descriptor!");
            throw;
        }
        if (!std::holds_alternative<BufferDescriptor>(_descriptor)) {
            gfx::log::error("Attempted to get range from a descriptor that does not hold a buffer!");
            throw;
        }
        return std::get<BufferDescriptor>(_descriptor)._range;
    }

    const ImageView & Descriptor::getImageView() const {
        if (!valid) {
            gfx::log::error("Attempted to get image view from an invalid descriptor!");
            throw;
        }
        if (std::holds_alternative<ImageDescriptor>(_descriptor)) {
            return *std::get<ImageDescriptor>(_descriptor)._imageView;
        }
        if (std::holds_alternative<CombinedImageSamplerDescriptor>(_descriptor)) {
            return *std::get<CombinedImageSamplerDescriptor>(_descriptor)._imageView;
        }
        gfx::log::error("Attempted to get image view from a descriptor that does not hold an image view!");
        throw;
    }

    const Sampler & Descriptor::getSampler() const {
        if (!valid) {
            gfx::log::error("Attempted to get sampler from an invalid descriptor!");
            throw;
        }
        if (std::holds_alternative<SamplerDescriptor>(_descriptor)) {
            return *std::get<SamplerDescriptor>(_descriptor)._sampler;
        }
        if (std::holds_alternative<CombinedImageSamplerDescriptor>(_descriptor)) {
            return *std::get<CombinedImageSamplerDescriptor>(_descriptor)._sampler;
        }
        gfx::log::error("Attempted to get sampler from a descriptor that does not hold a sampler!");
        throw;
    }


    DescriptorSet::Builder::Builder(const DescriptorSetLayout& layout) : layout(layout)
    {
        for (const auto& [binding, type, count] : layout.getBindings()) {
            writes[binding] = std::vector<Descriptor>();
            writes[binding].resize(count);
        }
    }

    DescriptorSet::Builder& DescriptorSet::Builder::write(const glm::u32 binding, const Descriptor& descriptor, const glm::u32 index)
    {
        if (!writes.contains(binding)) {
            gfx::log::error("Binding {} does not exist in the layout!", binding);
            throw;
        }
        if (index >= writes[binding].size()) {
            gfx::log::error("Index {} is out of bounds for binding {} which has count {}!", index, binding, writes[binding].size());
            throw;
        }
        if (writes[binding][index].isValid()) {
            gfx::log::error("Descriptor at binding {} index {} is already written!", binding, index);
            throw;
        }
        if (!descriptor.isValid()) {
            gfx::log::error("Descriptor at binding {} index {} is not valid!", binding, index);
            throw;
        }
        switch (layout.getBindingType(binding))
        {
        case DescriptorType::eUniformBuffer:
        case DescriptorType::eStorageBuffer:
            try {
                auto& _ = descriptor.getBuffer();
            } catch (const std::exception& e) {
                gfx::log::error("Descriptor for buffer binding {} index {} is invalid: {}", binding, index, e.what());
                throw;
            }
            break;
        case DescriptorType::eCombinedImageSampler:
            try {
                auto& _ = descriptor.getImageView();
            } catch (const std::exception& e) {
                gfx::log::error("Descriptor for combined image sampler binding {} index {} is invalid: {}", binding, index, e.what());
                throw;
            }
            try {
                auto& _ = descriptor.getSampler();
            } catch (const std::exception& e) {
                gfx::log::error("Descriptor for combined image sampler binding {} index {} is invalid: {}", binding, index, e.what());
                throw;
            }
            break;
        case DescriptorType::eSampledImage:
        case DescriptorType::eStorageImage:
            try {
                auto& _ = descriptor.getImageView();
            } catch (const std::exception& e) {
                gfx::log::error("Descriptor for image binding {} index {} is invalid: {}", binding, index, e.what());
                throw;
            }
            break;
        case DescriptorType::eSampler:
            try {
                auto& _ = descriptor.getSampler();
            } catch (const std::exception& e) {
                gfx::log::error("Descriptor for sampler binding {} index {} is invalid: {}", binding, index, e.what());
                throw;
            }
            break;
        default:
            gfx::log::error("Unknown descriptor type for binding {} index {}!", binding, index);
            throw;
        }

        writes[binding][index] = descriptor;
        return *this;
    }

    gfx::Resource<DescriptorSet> DescriptorSet::Builder::build()
    {
        switch (Context::Window().getAPI()) {
            case API::eOpenGL:
            return gfx::MakeResource<ogl::DescriptorSet>(*this);
            case API::eVulkan:
            return gfx::MakeResource<vk::DescriptorSet>(*this);
        default:
            throw std::runtime_error("Unknown graphics API!");
        }
    }

    DescriptorSet::DescriptorSet(const Builder& builder) : _layout(builder.layout), _writes(builder.writes)
    {
        _isPerFrame = false;
        for (const auto& write : _writes | std::views::values) {
            for (const auto& descriptor : write)
            {
                std::visit([this]<typename T0>(T0 binding)
                {
                    using T = std::decay_t<T0>;
                    if constexpr (std::is_same_v<BufferDescriptor, T>)
                    {
                        if (binding._buffer->isPerFrame()) {
                            _isPerFrame = true;
                        }
                    }
                    else if constexpr (std::is_same_v<ImageDescriptor, T> || std::is_same_v<CombinedImageSamplerDescriptor, T>)
                    {
                        if (binding._imageView->isPerFrame()) {
                            _isPerFrame = true;
                        }
                    }
                }, descriptor._descriptor);
            }
        }
    }
}
