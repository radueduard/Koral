//
// Created by radue on 6/10/2026.
//

module;

#include <stdexcept>
#include <glm/glm.hpp>

module gfx.descriptorBinding;
import gfx.buffer;
import gfx.log;

namespace gfx {
    Descriptor::Descriptor(const ResourceRef<const Buffer>& buffer, const glm::i64 offset, const glm::i64 range)
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

    Descriptor::Descriptor(const ResourceRef<const ImageView> &imageView, const ResourceRef<const Sampler> &sampler)
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

    Descriptor::Descriptor(const ResourceRef<const ImageView> &imageView)
        : valid(true), _descriptor(ImageDescriptor{ imageView })
    {
        if (!imageView) {
            gfx::log::error("Attempted to create an image descriptor with an invalid image view!");
            throw std::runtime_error("Image view must be valid!");
        }
    }

    Descriptor::Descriptor(const ResourceRef<const Sampler> &sampler)
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
}