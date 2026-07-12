//
// Created by radue on 3/6/2026.
//

#include "descriptorSet.h"

#include <ranges>
#include <iostream>

#include "accelerationStructure.h"
#include "buffer.h"
#include "context.h"
#include "descriptor.h"
#include "descriptorPool.h"
#include "descriptorSetLayout.h"
#include "device.h"
#include "imageView.h"
#include "sampler.h"
#include "vulkanContext.h"
#include <descriptorSetLayout.h>
#include <scheduler.h>

namespace kor::vk
{
    DescriptorSet::DescriptorSet(const Builder& builder) : kor::DescriptorSet(builder)
    {
        const auto frameCount = _isPerFrame ? kor::Context::Scheduler().getImageCount() : 1;
        for (size_t i = 0; i < frameCount; ++i) {
            _descriptorSets.emplace_back(Context::DescriptorPool().Allocate(dynamic_cast<const DescriptorSetLayout&>(*_layout)));
        }
        std::vector<::vk::WriteDescriptorSet> writes;
        std::vector<::vk::DescriptorBufferInfo> bufferInfos;
        std::vector<::vk::DescriptorImageInfo> imageInfos;
        std::vector<::vk::WriteDescriptorSetAccelerationStructureKHR> accelerationStructureInfos;
        std::vector<::vk::AccelerationStructureKHR> accelerationStructureHandles;

        size_t totalDescriptors = 0;
        for (const auto& descriptors : _writes | std::views::values) {
            totalDescriptors += descriptors.size();
        }

        for (int frame = 0; frame < frameCount; ++frame)
        {
            writes.reserve(totalDescriptors);
            bufferInfos.reserve(totalDescriptors);
            imageInfos.reserve(totalDescriptors);
            accelerationStructureInfos.reserve(totalDescriptors);
            accelerationStructureHandles.reserve(totalDescriptors);

            for (const auto& [binding, descriptors] : _writes)
            {
                const auto type = _layout->getBindingType(binding);
                for (size_t i = 0; i < descriptors.size(); ++i)
                {
                    const auto& descriptor = descriptors[i];
                    switch (type) {
                    case DescriptorType::eUniformBuffer:
                        {
                            const auto& vkBuffer = dynamic_cast<const kor::vk::Buffer&>(descriptor.getBuffer());
                            const auto& bufferHandle = vkBuffer.isPerFrame() ? vkBuffer[frame] : vkBuffer[0];
                            const auto& bufferInfo = bufferInfos.emplace_back()
                                .setBuffer(bufferHandle)
                                .setOffset(descriptor.getOffset())
                                .setRange(descriptor.getRange());
                            writes.emplace_back()
                                .setDstSet(_descriptorSets[frame])
                                .setDstBinding(binding)
                                .setDstArrayElement(i)
                                .setDescriptorType(::vk::DescriptorType::eUniformBuffer)
                                .setBufferInfo(bufferInfo);
                            break;
                        }
                    case DescriptorType::eStorageBuffer:
                        {
                            const auto& buffer = dynamic_cast<const Buffer&>(descriptor.getBuffer());
                            const auto& bufferHandle = buffer.isPerFrame() ? buffer[frame] : buffer[0];
                            const auto& bufferInfo = bufferInfos.emplace_back()
                                .setBuffer(bufferHandle)
                                .setOffset(descriptor.getOffset())
                                .setRange(descriptor.getRange());
                            writes.emplace_back()
                                .setDstSet(_descriptorSets[frame])
                                .setDstBinding(binding)
                                .setDstArrayElement(i)
                                .setDescriptorType(::vk::DescriptorType::eStorageBuffer)
                                .setBufferInfo(bufferInfo);
                            break;
                        }
                    case DescriptorType::eSampler:
                        {
                            const auto& sampler = dynamic_cast<const Sampler&>(descriptor.getSampler());
                            const auto& imageInfo = imageInfos.emplace_back()
                                .setSampler(*sampler);
                            writes.emplace_back()
                                .setDstSet(_descriptorSets[frame])
                                .setDstBinding(binding)
                                .setDstArrayElement(i)
                                .setDescriptorType(::vk::DescriptorType::eSampler)
                                .setImageInfo(imageInfo);
                            break;
                        }
                    case DescriptorType::eCombinedImageSampler:
                        {
                            const auto& sampler = dynamic_cast<const vk::Sampler&>(descriptor.getSampler());
                            const auto& imageView = dynamic_cast<const vk::ImageView&>(descriptor.getImageView());
                            const auto& imageViewHandle = imageView.isPerFrame() ? imageView[frame] : imageView[0];
                            const auto& imageInfo = imageInfos.emplace_back()
                                .setImageView(imageViewHandle)
                                .setSampler(*sampler)
                                .setImageLayout(::vk::ImageLayout::eShaderReadOnlyOptimal);

                            writes.emplace_back()
                                .setDstSet(_descriptorSets[frame])
                                .setDstBinding(binding)
                                .setDstArrayElement(i)
                                .setDescriptorType(::vk::DescriptorType::eCombinedImageSampler)
                                .setImageInfo(imageInfo);
                            break;
                        }
                    case DescriptorType::eStorageImage:
                        {
                            const auto& imageView = dynamic_cast<const ImageView&>(descriptor.getImageView());
                            const auto& imageViewHandle = imageView.isPerFrame() ? imageView[frame] : imageView[0];
                            const auto& imageInfo = imageInfos.emplace_back()
                                .setImageView(imageViewHandle)
                                .setImageLayout(::vk::ImageLayout::eGeneral);
                            writes.emplace_back()
                                .setDstSet(_descriptorSets[frame])
                                .setDstBinding(binding)
                                .setDstArrayElement(i)
                                .setDescriptorType(::vk::DescriptorType::eStorageImage)
                                .setImageInfo(imageInfo);
                            break;
                        }
                    case DescriptorType::eSampledImage:
                        {
                            const auto& imageView = dynamic_cast<const ImageView&>(descriptor.getImageView());
                            const auto& imageViewHandle = imageView.isPerFrame() ? imageView[frame] : imageView[0];
                            const auto& imageInfo = imageInfos.emplace_back()
                                .setImageView(imageViewHandle)
                                .setImageLayout(::vk::ImageLayout::eShaderReadOnlyOptimal);
                            writes.emplace_back()
                                .setDstSet(_descriptorSets[frame])
                                .setDstBinding(binding)
                                .setDstArrayElement(i)
                                .setDescriptorType(::vk::DescriptorType::eSampledImage)
                                .setImageInfo(imageInfo);
                            break;
                        }
                    case DescriptorType::eAccelerationStructure:
                        {
                            const auto& accelerationStructure = dynamic_cast<const AccelerationStructure&>(descriptor.getAccelerationStructure());
                            const auto& handle = accelerationStructureHandles.emplace_back(*accelerationStructure);
                            const auto& accelerationStructureInfo = accelerationStructureInfos.emplace_back()
                                .setAccelerationStructures(handle);
                            writes.emplace_back()
                                .setDstSet(_descriptorSets[frame])
                                .setDstBinding(binding)
                                .setDstArrayElement(i)
                                .setDescriptorType(::vk::DescriptorType::eAccelerationStructureKHR)
                                .setDescriptorCount(1)
                                .setPNext(&accelerationStructureInfo);
                            break;
                        }
                    default:
                        throw std::runtime_error("Unknown descriptor type for binding " + std::to_string(binding) + " index " + std::to_string(i) + "!");
                    }
                }
            }

            Context::Device()->updateDescriptorSets(writes, {});
            writes.clear();
            bufferInfos.clear();
            imageInfos.clear();
            accelerationStructureInfos.clear();
            accelerationStructureHandles.clear();
        }
    }

    DescriptorSet::~DescriptorSet()
    {
        for (const auto& descriptorSet : _descriptorSets) {
            Context::DescriptorPool().Free(descriptorSet);
        }
    }

    void DescriptorSet::Write(const glm::u32 binding, const Descriptor &descriptor, const glm::u32 index)
    {
        const auto frameCount = _isPerFrame ? kor::Context::Scheduler().getImageCount() : 1;
        for (int frame = 0; frame < frameCount; ++frame) {
            const auto type = _layout->getBindingType(binding);
            std::vector<::vk::WriteDescriptorSet> writes;
            std::vector<::vk::DescriptorBufferInfo> bufferInfos;
            std::vector<::vk::DescriptorImageInfo> imageInfos;
            switch (type) {
                case DescriptorType::eUniformBuffer:
                {
                    const auto& buffer = dynamic_cast<const Buffer&>(descriptor.getBuffer());
                    const auto& bufferHandle = buffer.isPerFrame() ? buffer[frame] : buffer[0];
                    const auto& bufferInfo = bufferInfos.emplace_back()
                        .setBuffer(bufferHandle)
                        .setOffset(descriptor.getOffset())
                        .setRange(descriptor.getRange());
                    writes.emplace_back()
                        .setDstSet(_descriptorSets[frame])
                        .setDstBinding(binding)
                        .setDstArrayElement(index)
                        .setDescriptorType(::vk::DescriptorType::eUniformBuffer)
                        .setBufferInfo(bufferInfo);
                    break;
                }
                case DescriptorType::eStorageBuffer:
                {
                    const auto& buffer = dynamic_cast<const Buffer&>(descriptor.getBuffer());
                    const auto& bufferHandle = buffer.isPerFrame() ? buffer[frame] : buffer[0];
                    const auto& bufferInfo = bufferInfos.emplace_back()
                        .setBuffer(bufferHandle)
                        .setOffset(descriptor.getOffset())
                        .setRange(descriptor.getRange());
                    writes.emplace_back()
                        .setDstSet(_descriptorSets[frame])
                        .setDstBinding(binding)
                        .setDstArrayElement(index)
                        .setDescriptorType(::vk::DescriptorType::eStorageBuffer)
                        .setBufferInfo(bufferInfo);
                    break;
                }
                case DescriptorType::eSampler:
                {
                    const auto& sampler = dynamic_cast<const Sampler&>(descriptor.getSampler());
                    const auto& imageInfo = imageInfos.emplace_back()
                        .setSampler(*sampler);
                    writes.emplace_back()
                        .setDstSet(_descriptorSets[frame])
                        .setDstBinding(binding)
                        .setDstArrayElement(index)
                        .setDescriptorType(::vk::DescriptorType::eSampler)
                        .setImageInfo(imageInfo);
                    break;
                }
                case DescriptorType::eCombinedImageSampler:
                {
                    const auto& sampler = dynamic_cast<const vk::Sampler&>(descriptor.getSampler());
                    const auto& imageView = dynamic_cast<const vk::ImageView&>(descriptor.getImageView());
                    const auto& imageViewHandle = imageView.isPerFrame() ? imageView[frame] : imageView[0];
                    const auto& imageInfo = imageInfos.emplace_back()
                        .setImageView(imageViewHandle)
                        .setSampler(*sampler)
                        .setImageLayout(::vk::ImageLayout::eShaderReadOnlyOptimal);
                    writes.emplace_back()
                        .setDstSet(_descriptorSets[frame])
                        .setDstBinding(binding)
                        .setDstArrayElement(index)
                        .setDescriptorType(::vk::DescriptorType::eCombinedImageSampler)
                        .setImageInfo(imageInfo);
                    break;
                }
                case DescriptorType::eStorageImage:
                {
                    const auto& imageView = dynamic_cast<const ImageView&>(descriptor.getImageView());
                    const auto& imageViewHandle = imageView.isPerFrame() ? imageView[frame] : imageView[0];
                    const auto& imageInfo = imageInfos.emplace_back()
                        .setImageView(imageViewHandle)
                        .setImageLayout(::vk::ImageLayout::eGeneral);
                    writes.emplace_back()
                        .setDstSet(_descriptorSets[frame])
                        .setDstBinding(binding)
                        .setDstArrayElement(index)
                        .setDescriptorType(::vk::DescriptorType::eStorageImage)
                        .setImageInfo(imageInfo);
                    break;
                }
                case DescriptorType::eSampledImage:
                {
                    const auto& imageView = dynamic_cast<const ImageView&>(descriptor.getImageView());
                    const auto& imageViewHandle = imageView.isPerFrame() ? imageView[frame] : imageView[0];
                    const auto& imageInfo = imageInfos.emplace_back()
                        .setImageView(imageViewHandle)
                        .setImageLayout(::vk::ImageLayout::eShaderReadOnlyOptimal);
                    writes.emplace_back()
                        .setDstSet(_descriptorSets[frame])
                        .setDstBinding(binding)
                        .setDstArrayElement(index)
                        .setDescriptorType(::vk::DescriptorType::eSampledImage)
                        .setImageInfo(imageInfo);
                    break;
                }
                case DescriptorType::eAccelerationStructure:
                {
                    const auto& accelerationStructure = dynamic_cast<const AccelerationStructure&>(descriptor.getAccelerationStructure());
                    const auto handle = *accelerationStructure;
                    const auto accelerationStructureInfo = ::vk::WriteDescriptorSetAccelerationStructureKHR()
                        .setAccelerationStructures(handle);
                    writes.emplace_back()
                        .setDstSet(_descriptorSets[frame])
                        .setDstBinding(binding)
                        .setDstArrayElement(index)
                        .setDescriptorType(::vk::DescriptorType::eAccelerationStructureKHR)
                        .setDescriptorCount(1)
                        .setPNext(&accelerationStructureInfo);
                    Context::Device()->updateDescriptorSets(writes, {});
                    continue;
                }
                default: throw std::runtime_error("Unknown descriptor type for binding " + std::to_string(binding) + " index " + std::to_string(index) + "!");
            }
            Context::Device()->updateDescriptorSets(writes, {});
        }
    }

    void DescriptorSet::DebugPrint() const {
        kor::DescriptorSet::DebugPrint();
        const auto frame = _isPerFrame ? kor::Context::Scheduler().getCurrentImageIndex() : 0;
        std::cout << "Current Vulkan descriptor set handle: " << _descriptorSets[frame] << std::endl;
    }

    ::vk::DescriptorSet DescriptorSet::operator*() const
    {
        const auto frameIndex = _isPerFrame ? kor::Context::Scheduler().getCurrentImageIndex() : 0;
        return _descriptorSets[frameIndex];
    }
}
