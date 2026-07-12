//
// Created by radue on 2/20/2026.
//

#pragma once

#include <variant>
#include <optional>

#include "api.h"
#include "resource.h"
#include "error.h"

namespace kor
{
    class Buffer;
    class Sampler;
    class ImageView;
    class AccelerationStructure;

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

    struct AccelerationStructureDescriptor {
        ResourceRef<const AccelerationStructure> _accelerationStructure;
    };

    class KORAL_API Descriptor
    {
        friend class DescriptorSet;
    public:
        Descriptor() = default;
        explicit Descriptor(const ResourceRef<const Buffer>& buffer, glm::i64 offset = 0, glm::i64 range = 0);
        explicit Descriptor(const ResourceRef<const ImageView>& imageView, const ResourceRef<const Sampler>& sampler);
        explicit Descriptor(const ResourceRef<const ImageView>& imageView);
        explicit Descriptor(const ResourceRef<const Sampler>& sampler);
        explicit Descriptor(const ResourceRef<const AccelerationStructure>& accelerationStructure);

        Descriptor(const Descriptor& other) = default;
        Descriptor& operator=(const Descriptor& other) = default;

        ~Descriptor() = default;
        [[nodiscard]] bool isValid() const { return valid; }
        // The reason this descriptor is invalid, if any (set instead of throwing from
        // the constructors); surfaced by DescriptorSet::Builder::build().
        [[nodiscard]] const std::optional<Error>& error() const { return _error; }

        [[nodiscard]] const Buffer& getBuffer() const;
        [[nodiscard]] glm::i64 getOffset() const;
        [[nodiscard]] glm::i64 getRange() const;
        [[nodiscard]] const ImageView& getImageView() const;
        [[nodiscard]] const Sampler& getSampler() const;
        [[nodiscard]] const AccelerationStructure& getAccelerationStructure() const;

    protected:
        bool valid = false;
        std::optional<Error> _error;
        std::variant<
            std::nullptr_t,
            BufferDescriptor,
            ImageDescriptor,
            SamplerDescriptor,
            CombinedImageSamplerDescriptor,
            AccelerationStructureDescriptor
        > _descriptor;
    };
}

