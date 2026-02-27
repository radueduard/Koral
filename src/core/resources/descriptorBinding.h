//
// Created by radue on 2/20/2026.
//

#pragma once

#include "buffer.h"
#include "imageView.h"
#include "sampler.h"

namespace gfx
{
    class Descriptor
    {
    public:
        enum class Type {
            eUniformBuffer,
            eStorageBuffer,
            eCombinedImageSampler,
            eStorageImage,
            eSampler,
        };

        virtual ~Descriptor() = default;
        virtual void Bind(glm::u32 bindingPoint) const = 0;

        struct CreateInfo
        {
            Type type;
            union
            {
                struct
                {
                    const Buffer* buffer;
                    glm::u32 offset;
                    glm::u32 range;
                };
                struct
                {
                    const ImageView* imageView;
                    const Sampler* sampler;
                };
            };

            CreateInfo& setType(const Type type) {
                this->type = type;
                return *this;
            }

            CreateInfo& setBuffer(const Buffer* buffer, const glm::u32 offset = 0, glm::u32 range = 0) {
                if (range == 0)
                {
                    range = buffer->getSize() - offset;
                }

                this->buffer = buffer;
                this->offset = offset;
                this->range = range;
                return *this;
            }

            /**
             * @brief Sets the image view and sampler for this descriptor. The imageView and sampler fields are used
             * differently depending on the type of descriptor being created.
             *
             * @attention The behavior of this function is determined by the type field of the create info struct.
             * It is the caller's responsibility to ensure that the type field is set correctly before calling this
             * function, and that the appropriate fields are set based on the type. At least one of the imageView or
             * sampler fields must be set to a non-nullptr value, depending on the type of descriptor being created.
             *
             * @param imageView is used for both combined image sampler and storage image descriptors, the difference
             * is determined by the type field of the create info struct. If the type is set to eCombinedImageSampler,
             * the sampler field must also be set to a valid sampler. If the type is set to eStorageImage,
             * the sampler field is ignored and can be set to nullptr. If the type is set to eSampler, the imageView
             * field is ignored and can be set to nullptr.
             *
             * @param sampler is used for combined image sampler and sampler descriptors. If the imageView field of the
             * create info struct is set to nullptr, the sampler field is ignored and can be set to nullptr as well.
             *
             */
            CreateInfo& setImage(const ImageView* imageView, const Sampler* sampler = nullptr) {
                this->imageView = imageView;
                this->sampler = sampler;
                return *this;
            }
        };

        static std::unique_ptr<Descriptor> Create(const CreateInfo& createInfo);

    protected:
        explicit Descriptor(const CreateInfo& createInfo);

        Type _type = Type::eUniformBuffer;
        union
        {
            struct
            {
                const Buffer* _buffer;
                glm::i64 _offset;
                glm::i64 _range;
            };
            struct
            {
                const ImageView* _imageView;
                const Sampler* _sampler;
            };
        };
    };
}

