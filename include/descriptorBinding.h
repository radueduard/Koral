//
// Created by radue on 2/20/2026.
//

#pragma once

#include <variant>

#include "api.h"
#include "resource.h"

namespace gfx
{
    class Buffer;
    class Sampler;
    class ImageView;

    struct BufferDescriptor {
        ResourceRef<Buffer> _buffer;
        glm::i64 _offset = 0;
        glm::i64 _range = 0;
    };

    struct ImageDescriptor {
        ResourceRef<ImageView> _imageView;
    };

    struct SamplerDescriptor {
        ResourceRef<Sampler> _sampler;
    };

    struct CombinedImageSamplerDescriptor {
        ResourceRef<ImageView> _imageView;
        ResourceRef<Sampler> _sampler;
    };

    class GFX_API Descriptor
    {
    public:
        Descriptor() = default;
        explicit Descriptor(const ResourceRef<Buffer>& buffer, glm::i64 offset = 0, glm::i64 range = 0);
        explicit Descriptor(const ResourceRef<ImageView>& imageView, const ResourceRef<Sampler>& sampler);
        explicit Descriptor(const ResourceRef<ImageView>& imageView);
        explicit Descriptor(const ResourceRef<Sampler>& sampler);

        Descriptor(const Descriptor& other) = default;
        Descriptor& operator=(const Descriptor& other) = default;

        ~Descriptor() = default;
        [[nodiscard]] bool isValid() const { return valid; }

        [[nodiscard]] const Buffer& getBuffer() const;
        [[nodiscard]] glm::i64 getOffset() const;
        [[nodiscard]] glm::i64 getRange() const;
        [[nodiscard]] const ImageView& getImageView() const;
        [[nodiscard]] const Sampler& getSampler() const;

    protected:
        bool valid = false;
        std::variant<
            std::nullptr_t,
            BufferDescriptor,
            ImageDescriptor,
            SamplerDescriptor,
            CombinedImageSamplerDescriptor
        > _descriptor;
    };
}

