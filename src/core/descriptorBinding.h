//
// Created by radue on 2/20/2026.
//

#pragma once

#include "buffer.h"
#include "descriptorSet.h"
#include "imageView.h"
#include "sampler.h"

namespace gfx
{
    class Descriptor
    {
        friend class DescriptorSet::Builder;
    public:
        Descriptor() = default;
        explicit Descriptor(const Buffer* buffer, glm::i64 offset = 0, glm::i64 range = 0) : valid(true), _buffer(buffer), _offset(offset), _range(range)
        {
            if (buffer == nullptr) {
                throw std::runtime_error("Buffer must be valid!");
            }

            if (offset < 0 || offset >= buffer->getSize()) {
                throw std::runtime_error("Offset must be within the bounds of the buffer!");
            }

            if (range < 0) {
                throw std::runtime_error("Range must be non-negative!");
            }

            if (offset + range > buffer->getSize()) {
                throw std::runtime_error("Offset + range must be within the bounds of the buffer!");
            }

            if (range == 0) {
                _range = buffer->getSize() - offset;
            }
        }
        explicit Descriptor(const ImageView* imageView, const Sampler* sampler) : valid(true), _imageView(imageView), _sampler(sampler) {}

        Descriptor(const Descriptor& other) = default;
        Descriptor& operator=(const Descriptor& other) = default;

        ~Descriptor() = default;
        [[nodiscard]] bool isValid() const { return valid; }

        [[nodiscard]] const Buffer* getBuffer() const { return _buffer; }
        [[nodiscard]] glm::i64 getOffset() const { return _offset; }
        [[nodiscard]] glm::i64 getRange() const { return _range; }
        [[nodiscard]] const ImageView* getImageView() const { return _imageView; }
        [[nodiscard]] const Sampler* getSampler() const { return _sampler; }

    protected:
        bool valid = false;
        const Buffer* _buffer = nullptr;
        glm::i64 _offset = 0;
        glm::i64 _range = 0;
        const ImageView* _imageView = nullptr;
        const Sampler* _sampler = nullptr;
    };
}

