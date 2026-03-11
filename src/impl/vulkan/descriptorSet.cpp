//
// Created by radue on 3/6/2026.
//

#include "descriptorSet.h"

#include <ranges>

#include "buffer.h"
#include "descriptorPool.h"
#include "descriptorSetLayout.h"
#include "device.h"
#include "imageView.h"
#include "sampler.h"
#include "vulkanContext.h"
#include "core/descriptorSetLayout.h"

namespace gfx::vk
{
    DescriptorSet::DescriptorSet(const Builder& builder) : gfx::DescriptorSet(builder)
    {
        _handle = Context::DescriptorPool().Allocate(dynamic_cast<const DescriptorSetLayout&>(_layout));
        std::vector<::vk::WriteDescriptorSet> writes;
        std::vector<::vk::DescriptorBufferInfo> bufferInfos;
        std::vector<::vk::DescriptorImageInfo> imageInfos;

        // Pre-calculate total descriptor count to reserve space and prevent reallocation
        // which would invalidate references stored in WriteDescriptorSet
        size_t totalDescriptors = 0;
        for (const auto& descriptors : _writes | std::views::values) {
            totalDescriptors += descriptors.size();
        }
        writes.reserve(totalDescriptors);
        bufferInfos.reserve(totalDescriptors);
        imageInfos.reserve(totalDescriptors);

        for (const auto& [binding, descriptors] : _writes)
        {
            const auto type = _layout.getBindingType(binding);
            for (size_t i = 0; i < descriptors.size(); ++i)
            {
                const auto& descriptor = descriptors[i];
                switch (type) {
                case DescriptorType::eUniformBuffer:
                    {
                        auto* buffer = dynamic_cast<const gfx::vk::Buffer*>(descriptor.getBuffer());
                        const auto& bufferInfo = bufferInfos.emplace_back()
                            .setBuffer(**buffer)
                            .setOffset(descriptor.getOffset())
                            .setRange(descriptor.getRange());
                        writes.emplace_back()
                            .setDstSet(_handle)
                            .setDstBinding(binding)
                            .setDstArrayElement(i)
                            .setDescriptorType(::vk::DescriptorType::eUniformBuffer)
                            .setBufferInfo(bufferInfo);
                        break;
                    }
                case DescriptorType::eSampler:
                    {
                        auto* sampler = dynamic_cast<const Sampler*>(descriptor.getSampler());
                        const auto& imageInfo = imageInfos.emplace_back()
                            .setSampler(**sampler);
                        writes.emplace_back()
                            .setDstSet(_handle)
                            .setDstBinding(binding)
                            .setDstArrayElement(i)
                            .setDescriptorType(::vk::DescriptorType::eSampler)
                            .setImageInfo(imageInfo);
                        break;
                    }
                case DescriptorType::eCombinedImageSampler:
                    {
                        auto* sampler = dynamic_cast<const vk::Sampler*>(descriptor.getSampler());
                        auto* imageView = dynamic_cast<const vk::ImageView*>(descriptor.getImageView());
                        const auto& imageInfo = imageInfos.emplace_back()
                            .setImageView(**imageView)
                            .setSampler(**sampler)
                            .setImageLayout(dynamic_cast<const Image&>(imageView->getImage()).getImageLayout());

                        writes.emplace_back()
                            .setDstSet(_handle)
                            .setDstBinding(binding)
                            .setDstArrayElement(i)
                            .setDescriptorType(::vk::DescriptorType::eCombinedImageSampler)
                            .setImageInfo(imageInfo);
                        break;
                    }
                case DescriptorType::eStorageBuffer:
                    {
                        auto* buffer = dynamic_cast<const Buffer*>(descriptor.getBuffer());
                        const auto& bufferInfo = bufferInfos.emplace_back()
                            .setBuffer(**buffer)
                            .setOffset(descriptor.getOffset())
                            .setRange(descriptor.getRange());
                        writes.emplace_back()
                            .setDstSet(_handle)
                            .setDstBinding(binding)
                            .setDstArrayElement(i)
                            .setDescriptorType(::vk::DescriptorType::eStorageBuffer)
                            .setBufferInfo(bufferInfo);
                        break;
                    }
                case DescriptorType::eStorageImage:
                    {
                        auto* imageView = dynamic_cast<const ImageView*>(descriptor.getImageView());
                        const auto& imageInfo = imageInfos.emplace_back()
                            .setImageView(**imageView)
                            .setImageLayout(dynamic_cast<const Image&>(imageView->getImage()).getImageLayout());
                        writes.emplace_back()
                            .setDstSet(_handle)
                            .setDstBinding(binding)
                            .setDstArrayElement(i)
                            .setDescriptorType(::vk::DescriptorType::eStorageImage)
                            .setImageInfo(imageInfo);
                        break;
                    }

                default:
                    throw std::runtime_error("Unknown descriptor type for binding " + std::to_string(binding) + " index " + std::to_string(i) + "!");
                }
            }
        }

        Context::Device()->updateDescriptorSets(writes, {});
    }

    DescriptorSet::~DescriptorSet()
    {
        if (_handle) {
            Context::DescriptorPool().Free(_handle);
        }
    }

    void DescriptorSet::bind(const gfx::CommandBuffer& commandBuffer, glm::u32 index) const
    {
        const auto& vkCommandBuffer = dynamic_cast<const CommandBuffer&>(commandBuffer);
    }
}
