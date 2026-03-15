//
// Created by radue on 2/20/2026.
//

#pragma once

#include "api.h"

namespace gfx
{
    class Buffer;
    class Sampler;
    class ImageView;

    class GFX_API Descriptor
    {
    public:
        Descriptor() = default;
        explicit Descriptor(const Buffer* buffer, glm::i64 offset = 0, glm::i64 range = 0);
        explicit Descriptor(const ImageView* imageView, const Sampler* sampler);

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
        const gfx::Buffer* _buffer = nullptr;
        glm::i64 _offset = 0;
        glm::i64 _range = 0;
        const ImageView* _imageView = nullptr;
        const Sampler* _sampler = nullptr;
    };
}

