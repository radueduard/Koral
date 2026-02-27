//
// Created by radue on 2/20/2026.
//
#include "descriptorBinding.h"

#include <glm/fwd.hpp>

namespace gfx::ogl
{

    Descriptor::Descriptor(const Builder& createInfo) : gfx::Descriptor(createInfo) {}

    void Descriptor::Bind(const glm::u32 bindingPoint) const
    {
        switch (_type)
        {
        case Type::eUniformBuffer:
            {
                auto* buffer = dynamic_cast<const ogl::Buffer*>(_buffer);
                const auto bufferName = **buffer;
                glBindBufferRange(GL_UNIFORM_BUFFER, bindingPoint, bufferName, _offset, _range);
                glCheckError();
                break;
            }
        case Type::eStorageBuffer:
            {
                auto* buffer = dynamic_cast<const ogl::Buffer*>(_buffer);
                glBindBufferRange(GL_SHADER_STORAGE_BUFFER, bindingPoint, **buffer, _offset, _range);
                glCheckError();
                break;
            }
        case Type::eCombinedImageSampler:
            {
                auto* sampler = dynamic_cast<const ogl::Sampler*>(_sampler);
                auto* imageView = dynamic_cast<const ogl::ImageView*>(_imageView);

                glBindSampler(bindingPoint, **sampler);
                glCheckError();
                glActiveTexture(GL_TEXTURE0 + bindingPoint);
                glBindTexture(GL_TEXTURE_2D, **imageView);
                glCheckError();
                break;
            }
        case Type::eStorageImage:
            {
                auto* imageView = dynamic_cast<const ogl::ImageView*>(_imageView);

                glBindImageTexture(bindingPoint,
                    **imageView,
                    imageView->GetBaseMipLevel(),
                    imageView->GetArrayLayerCount() > 1,
                    imageView->GetBaseArrayLayer(),
                    GL_READ_WRITE,
                    imageView->getFormat());
                glCheckError();
                break;
            }
        case Type::eSampler:
            {
                auto* sampler = dynamic_cast<const ogl::Sampler*>(_sampler);

                glBindSampler(bindingPoint, **sampler);
                glCheckError();
                break;
            }
        }
    }
}

