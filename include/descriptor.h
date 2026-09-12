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
    class BufferView;

    /** @brief A buffer bound to a shader, and which part of it. */
    struct BufferDescriptor {
        ResourceRef<const Buffer> _buffer;  ///< The buffer.
        glm::i64 _offset = 0;               ///< Byte offset the shader's view of it starts at.
        glm::i64 _range = 0;                ///< How many bytes it covers; 0 means the rest of the buffer.
    };

    /** @brief An image bound to a shader without a sampler — a storage image, or a separate sampled image. */
    struct ImageDescriptor {
        ResourceRef<const ImageView> _imageView;    ///< The view the shader reads or writes through.
    };

    /** @brief A sampler bound on its own, to be paired with a separate image in the shader. */
    struct SamplerDescriptor {
        ResourceRef<const Sampler> _sampler;    ///< The filtering and addressing rules.
    };

    /** @brief An image and the sampler to read it with, bound together — the ordinary texture binding. */
    struct CombinedImageSamplerDescriptor {
        ResourceRef<const ImageView> _imageView; ///< The texture.
        ResourceRef<const Sampler> _sampler;     ///< How it is filtered and addressed.
    };

    /** @brief A buffer bound as an array of formatted texels — what a `samplerBuffer` fetches from. */
    struct TexelBufferDescriptor {
        ResourceRef<const BufferView> _bufferView;  ///< The view saying how the bytes are read.
    };

    /** @brief A ray-tracing acceleration structure bound for a shader to trace against. */
    struct AccelerationStructureDescriptor {
        ResourceRef<const AccelerationStructure> _accelerationStructure;    ///< The structure, normally a TLAS.
    };

    /**
     * @brief One resource, ready to be bound at one binding of a descriptor set.
     *
     * Construct it from whatever the binding expects — the constructor overload picks the kind —
     * and hand it to DescriptorSet::Builder:
     *
     * @code
     * kor::Descriptor camera(cameraBuffer);              // uniform or storage buffer
     * kor::Descriptor albedo(albedoView, linearSampler); // combined image sampler
     * kor::Descriptor target(storageView);               // storage image
     * @endcode
     *
     * A descriptor built from an unusable resource does not throw; it becomes invalid and carries
     * the reason, which DescriptorSet::Builder::build() reports.
     */
    class KORAL_API Descriptor
    {
        friend class DescriptorSet;
    public:
        /** @brief An empty descriptor, binding nothing. Valid in a sparse bindless array. */
        Descriptor() = default;

        /**
         * @brief Binds a buffer, or part of one.
         * @param buffer The buffer.
         * @param offset Byte offset the shader's view of it starts at.
         * @param range How many bytes it covers; 0 means the rest of the buffer.
         */
        explicit Descriptor(const ResourceRef<const Buffer>& buffer, glm::i64 offset = 0, glm::i64 range = 0);

        /** @brief Binds a texture and the sampler to read it with. */
        explicit Descriptor(const ResourceRef<const ImageView>& imageView, const ResourceRef<const Sampler>& sampler);

        /** @brief Binds an image without a sampler — a storage image, or a separate sampled image. */
        explicit Descriptor(const ResourceRef<const ImageView>& imageView);

        /** @brief Binds a sampler on its own, for a shader that pairs it with a separately bound image. */
        explicit Descriptor(const ResourceRef<const Sampler>& sampler);

        /** @brief Binds an acceleration structure for a shader to trace rays against. */
        explicit Descriptor(const ResourceRef<const AccelerationStructure>& accelerationStructure);

        /** @brief Binds a buffer as an array of formatted texels. @see BufferView */
        explicit Descriptor(const ResourceRef<const BufferView>& bufferView);

        Descriptor(const Descriptor& other) = default;
        Descriptor& operator=(const Descriptor& other) = default;

        ~Descriptor() = default;

        /** @brief Whether it holds a usable resource. A false answer is explained by error(). */
        [[nodiscard]] bool isValid() const { return valid; }

        /**
         * @brief Why this descriptor is invalid, if it is.
         * @return The error, or nullopt when it is valid. Recorded rather than thrown, and surfaced
         *         by DescriptorSet::Builder::build().
         */
        [[nodiscard]] const std::optional<Error>& error() const { return _error; }

        /** @brief The bound buffer. @throws if this descriptor holds something else. */
        [[nodiscard]] const Buffer& getBuffer() const;
        /** @brief Byte offset into the bound buffer. @throws if this descriptor holds something else. */
        [[nodiscard]] glm::i64 getOffset() const;
        /** @brief How many bytes of the bound buffer are visible. @throws if this descriptor holds something else. */
        [[nodiscard]] glm::i64 getRange() const;
        /** @brief The bound image view. @throws if this descriptor holds something else. */
        [[nodiscard]] const ImageView& getImageView() const;
        /** @brief The bound sampler. @throws if this descriptor holds something else. */
        [[nodiscard]] const Sampler& getSampler() const;
        /** @brief The bound acceleration structure. @throws if this descriptor holds something else. */
        [[nodiscard]] const AccelerationStructure& getAccelerationStructure() const;
        /** @brief The bound buffer view. @throws if this descriptor holds something else. */
        [[nodiscard]] const BufferView& getBufferView() const;

        /**
         * @brief The bound buffer, or an empty reference if this descriptor holds something else.
         *
         * The non-throwing counterpart of getBuffer(), for code that walks a whole set without
         * knowing what each slot holds — the barrier resolver does exactly that, and a bindless
         * array is routinely sparse.
         */
        [[nodiscard]] ResourceRef<const Buffer> getBufferRef() const;

        /** @brief The bound image view, or an empty reference if this descriptor holds something else. */
        [[nodiscard]] ResourceRef<const ImageView> getImageViewRef() const;

        /** @brief The bound buffer view, or an empty reference if this descriptor holds something else. */
        [[nodiscard]] ResourceRef<const BufferView> getBufferViewRef() const;

    protected:
        bool valid = false;
        std::optional<Error> _error;
        std::variant<
            std::nullptr_t,
            BufferDescriptor,
            ImageDescriptor,
            SamplerDescriptor,
            CombinedImageSamplerDescriptor,
            AccelerationStructureDescriptor,
            TexelBufferDescriptor
        > _descriptor;
    };
}

