//
// Created by radue on 3/5/2026.
//

#include "descriptorSet.h"

#include "buffer.h"
#include "commandBuffer.h"
#include "imageView.h"
#include "sampler.h"
#include "core/comandBuffer.h"
#include "core/descriptorBinding.h"
#include "core/descriptorSetLayout.h"
#include "utils/ogl_err_handling.h"

namespace gfx
{
    namespace ogl
    {
        DescriptorSet::DescriptorSet(const Builder& builder): gfx::DescriptorSet(builder) {}

        void DescriptorSet::bind(const gfx::CommandBuffer& commandBuffer, glm::u32 index) const
        {
            const auto& oglCommandBuffer = dynamic_cast<const CommandBuffer&>(commandBuffer);
            const auto& layoutRemappings =  oglCommandBuffer.getRemappingTableForBoundPipeline();

            for (const auto& [binding, descriptors] : _writes)
            {
                const auto type = _layout.getBindingType(binding);
                for (size_t i = 0; i < descriptors.size(); ++i) {
                    const auto& descriptor = descriptors[i];

                    auto setBinding = std::make_pair(index, binding);
                    auto bindingPoint = layoutRemappings.find(setBinding);
                    if (bindingPoint == layoutRemappings.end()) {
                        throw std::runtime_error("No binding point found for set " + std::to_string(index) + " binding " + std::to_string(binding) + "!");
                    }
                    switch (type)
                    {
                    case DescriptorType::eUniformBuffer:
                        {
                            auto* buffer = dynamic_cast<const ogl::Buffer*>(descriptor.getBuffer());
                            glBindBufferRange(GL_UNIFORM_BUFFER, bindingPoint->second, **buffer, descriptor.getOffset(), descriptor.getRange());
                            glCheckError();
                            break;
                        }
                    case DescriptorType::eStorageBuffer:
                        {
                            auto* buffer = dynamic_cast<const ogl::Buffer*>(descriptor.getBuffer());
                            glBindBufferRange(GL_SHADER_STORAGE_BUFFER, bindingPoint->second, **buffer, descriptor.getOffset(), descriptor.getRange());
                            glCheckError();
                            break;
                        }
                    case DescriptorType::eCombinedImageSampler:
                        {
                            auto* sampler = dynamic_cast<const ogl::Sampler*>(descriptor.getSampler());
                            auto* imageView = dynamic_cast<const ogl::ImageView*>(descriptor.getImageView());

                            glBindSampler(bindingPoint->second, **sampler);
                            glCheckError();
                            glActiveTexture(GL_TEXTURE0 + bindingPoint->second);
                            glBindTexture(GL_TEXTURE_2D, **imageView);
                            glCheckError();
                            break;
                        }
                    case DescriptorType::eStorageImage:
                        {
                            auto* imageView = dynamic_cast<const ogl::ImageView*>(descriptor.getImageView());

                            glBindImageTexture(bindingPoint->second,
                                **imageView,
                                imageView->getBaseMipLevel(),
                                imageView->getArrayLayerCount() > 1,
                                imageView->getBaseArrayLayer(),
                                GL_READ_WRITE,
                                imageView->getFormat());
                            glCheckError();
                            break;
                        }
                    case DescriptorType::eSampler:
                        {
                            auto* sampler = dynamic_cast<const ogl::Sampler*>(descriptor.getSampler());
                            glBindSampler(bindingPoint->second, **sampler);
                            glCheckError();
                            break;
                        }
                    default:
                        throw std::runtime_error("Unknown descriptor type for binding " + std::to_string(binding) + " index " + std::to_string(i) + "!");
                    }
                }
            }
        }
    }
}
