//
// Created by radue on 2/20/2026.
//

module;

#include <glm/fwd.hpp>
#include "api.h"

export module gfx:descriptor;
import :types;

import std;
import resource;

namespace gfx
{
    struct BufferDescriptor {
        ResourceRef<const Buffer> _buffer;
        glm::i64 _offset = 0;
        glm::i64 _range = 0;
    };

    struct ImageDescriptor {
        ResourceRef<const ImageView> _imageView;
    };

    struct SamplerDescriptor {
        ResourceRef<const Sampler> _sampler;
    };

    struct CombinedImageSamplerDescriptor {
        ResourceRef<const ImageView> _imageView;
        ResourceRef<const Sampler> _sampler;
    };

    class GFX_API Descriptor
    {
    public:
        Descriptor() = default;
        explicit Descriptor(const ResourceRef<const Buffer>& buffer, glm::i64 offset = 0, glm::i64 range = 0);
        explicit Descriptor(const ResourceRef<const ImageView>& imageView, const ResourceRef<const Sampler>& sampler);
        explicit Descriptor(const ResourceRef<const ImageView>& imageView);
        explicit Descriptor(const ResourceRef<const Sampler>& sampler);

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

