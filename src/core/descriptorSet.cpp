//
// Created by radue on 3/4/2026.
//

#include <descriptorSet.h>
#include <descriptorBinding.h>
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
    Descriptor::Descriptor(const ResourceRef<Buffer>& buffer, const glm::i64 offset, const glm::i64 range)
        : valid(true), _descriptor(BufferDescriptor{ buffer, offset, range })
    {
        if (!buffer) {
            gfx::log::error("Attempted to create a buffer descriptor with an invalid buffer!");
            throw std::runtime_error("Buffer must be valid!");
        }

        if (offset < 0 || offset >= buffer->getSize()) {
            gfx::log::error("Attempted to create a buffer descriptor with an invalid offset! Buffer size: {}, offset: {}", buffer->getSize(), offset);
            throw std::runtime_error("Offset must be within the bounds of the buffer!");
        }

        if (range < 0) {
            gfx::log::error("Attempted to create a buffer descriptor with a negative range! Range: {}", range);
            throw std::runtime_error("Range must be non-negative!");
        }

        if (offset + range > buffer->getSize()) {
            gfx::log::error("Attempted to create a buffer descriptor with an offset + range that exceeds the buffer size! Buffer size: {}, offset: {}, range: {}", buffer->getSize(), offset, range);
            throw std::runtime_error("Offset + range must be within the bounds of the buffer!");
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

    Descriptor::Descriptor(const ResourceRef<ImageView> &imageView, const ResourceRef<Sampler> &sampler)
        : valid(true), _descriptor(CombinedImageSamplerDescriptor{ imageView, sampler })
    {
        if (!imageView) {
            gfx::log::error("Attempted to create a combined image sampler descriptor with an invalid image view!");
            throw std::runtime_error("Image view must be valid!");
        }
        if (!sampler) {
            gfx::log::error("Attempted to create a combined image sampler descriptor with an invalid sampler!");
            throw std::runtime_error("Sampler must be valid!");
        }
    }

    Descriptor::Descriptor(const ResourceRef<ImageView> &imageView)
        : valid(true), _descriptor(ImageDescriptor{ imageView })
    {
        if (!imageView) {
            gfx::log::error("Attempted to create an image descriptor with an invalid image view!");
            throw std::runtime_error("Image view must be valid!");
        }
    }

    Descriptor::Descriptor(const ResourceRef<Sampler> &sampler)
        : valid(true), _descriptor(SamplerDescriptor{ sampler }) {
        if (!sampler) {
            gfx::log::error("Attempted to create a sampler descriptor with an invalid sampler!");
            throw std::runtime_error("Sampler must be valid!");
        }
    }

    const Buffer & Descriptor::getBuffer() const {
        if (!valid) {
            throw std::runtime_error("Descriptor is not valid!");
        }
        if (!std::holds_alternative<BufferDescriptor>(_descriptor)) {
            throw std::runtime_error("Descriptor does not hold a buffer!");
        }
        return *std::get<BufferDescriptor>(_descriptor)._buffer;
    }

    glm::i64 Descriptor::getOffset() const {
        if (!valid) {
            throw std::runtime_error("Descriptor is not valid!");
        }
        if (!std::holds_alternative<BufferDescriptor>(_descriptor)) {
            throw std::runtime_error("Descriptor does not hold a buffer!");
        }
        return std::get<BufferDescriptor>(_descriptor)._offset;
    }

    glm::i64 Descriptor::getRange() const {
        if (!valid) {
            throw std::runtime_error("Descriptor is not valid!");
        }
        if (!std::holds_alternative<BufferDescriptor>(_descriptor)) {
            throw std::runtime_error("Descriptor does not hold a buffer!");
        }
        return std::get<BufferDescriptor>(_descriptor)._range;
    }

    const ImageView & Descriptor::getImageView() const {
        if (!valid) {
            throw std::runtime_error("Descriptor is not valid!");
        }
        if (std::holds_alternative<ImageDescriptor>(_descriptor)) {
            return *std::get<ImageDescriptor>(_descriptor)._imageView;
        }
        if (std::holds_alternative<CombinedImageSamplerDescriptor>(_descriptor)) {
            return *std::get<CombinedImageSamplerDescriptor>(_descriptor)._imageView;
        }
        throw std::runtime_error("Descriptor does not hold an image view!");
    }

    const Sampler & Descriptor::getSampler() const {
        if (!valid) {
            throw std::runtime_error("Descriptor is not valid!");
        }
        if (std::holds_alternative<SamplerDescriptor>(_descriptor)) {
            return *std::get<SamplerDescriptor>(_descriptor)._sampler;
        }
        if (std::holds_alternative<CombinedImageSamplerDescriptor>(_descriptor)) {
            return *std::get<CombinedImageSamplerDescriptor>(_descriptor)._sampler;
        }
        throw std::runtime_error("Descriptor does not hold a sampler!");
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
            throw std::runtime_error("Binding " + std::to_string(binding) + " does not exist in the layout!");
        }
        if (index >= writes[binding].size()) {
            throw std::runtime_error("Index " + std::to_string(index) + " is out of bounds for binding " + std::to_string(binding) + " which has count " + std::to_string(writes[binding].size()) + "!");
        }
        if (writes[binding][index].isValid()) {
            throw std::runtime_error("Descriptor at binding " + std::to_string(binding) + " index " + std::to_string(index) + " is already written!");
        }
        if (!descriptor.isValid()) {
            throw std::runtime_error("Descriptor is not valid!");
        }
        switch (layout.getBindingType(binding))
        {
        case DescriptorType::eUniformBuffer:
        case DescriptorType::eStorageBuffer:
            try {
                auto& _ = descriptor.getBuffer();
            } catch (const std::exception& e) {
                gfx::log::error("Descriptor for buffer binding {} index {} is invalid: {}", binding, index, e.what());
                throw std::runtime_error("Descriptor for buffer binding " + std::to_string(binding) + " index " + std::to_string(index) + " must have a valid buffer!");
            }
            break;
        case DescriptorType::eCombinedImageSampler:
            try {
                auto& _ = descriptor.getImageView();
            } catch (const std::exception& e) {
                gfx::log::error("Descriptor for combined image sampler binding {} index {} is invalid: {}", binding, index, e.what());
                throw std::runtime_error("Descriptor for combined image sampler binding " + std::to_string(binding) + " index " + std::to_string(index) + " must have a valid image view!");
            }
            try {
                auto& _ = descriptor.getSampler();
            } catch (const std::exception& e) {
                gfx::log::error("Descriptor for combined image sampler binding {} index {} is invalid: {}", binding, index, e.what());
                throw std::runtime_error("Descriptor for combined image sampler binding " + std::to_string(binding) + " index " + std::to_string(index) + " must have a valid sampler!");
            }
            break;
        case DescriptorType::eSampledImage:
        case DescriptorType::eStorageImage:
            try {
                auto& _ = descriptor.getImageView();
            } catch (const std::exception& e) {
                gfx::log::error("Descriptor for image binding {} index {} is invalid: {}", binding, index, e.what());
                throw std::runtime_error("Descriptor for image binding " + std::to_string(binding) + " index " + std::to_string(index) + " must have a valid image view!");
            }
            break;
        case DescriptorType::eSampler:
            try {
                auto& _ = descriptor.getSampler();
            } catch (const std::exception& e) {
                gfx::log::error("Descriptor for sampler binding {} index {} is invalid: {}", binding, index, e.what());
                throw std::runtime_error("Descriptor for sampler binding " + std::to_string(binding) + " index " + std::to_string(index) + " must have a valid sampler!");
            }
            break;
        default:
            throw std::runtime_error("Unknown descriptor type for binding " + std::to_string(binding) + " index " + std::to_string(index) + "!");
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
                try {
                    if (const auto& buffer = descriptor.getBuffer(); buffer.isPerFrame()) {
                        _isPerFrame = true;
                        break;
                    }
                } catch (const std::exception&) {
                    // Not a buffer descriptor, ignore
                }
                try {
                    if (const auto imageView = descriptor.getImageView(); imageView.isPerFrame()) {
                        _isPerFrame = true;
                        break;
                    }
                } catch (const std::exception&) {
                    // Not an image view descriptor, ignore
                }
            }
        }
    }
}
