//
// Created by radue on 2/27/2026.
//

#include "vulkanContext.h"

#include "allocator.h"
#include "descriptorPool.h"
#include "device.h"
#include "runtime.h"
#include "scheduler.h"

const kor::vk::Runtime& kor::vk::Context::Runtime()
{
    if (!_runtime)
        throw std::runtime_error("Runtime is not initialized");
    return *_runtime;
}

const kor::vk::Device& kor::vk::Context::Device()
{
    if (!_device)
        throw std::runtime_error("Device is not initialized");
    return *_device;
}

const kor::vk::Allocator& kor::vk::Context::Allocator()
{
    if (!_allocator)
        throw std::runtime_error("Allocator is not initialized");
    return *_allocator;
}

const kor::vk::DescriptorPool& kor::vk::Context::DescriptorPool()
{
    if (!_descriptorPool)
        throw std::runtime_error("DescriptorPool is not initialized");
    return *_descriptorPool;
}

void initDispatcher();

void kor::vk::Context::Init()
{
    initDispatcher();

    _runtime = new kor::vk::Runtime;
    _runtime->selectPhysicalDevice();
    _device = new kor::vk::Device;
    _allocator = new kor::vk::Allocator;
    auto descriptorPoolBuilder = kor::vk::DescriptorPool::Builder()
        .setMaxSets(1000)
        .setPoolFlags(::vk::DescriptorPoolCreateFlagBits::eUpdateAfterBind | ::vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet)
        .addPoolSize(::vk::DescriptorType::eUniformBuffer, 1000)
        .addPoolSize(::vk::DescriptorType::eSampledImage, 1000)
        .addPoolSize(::vk::DescriptorType::eStorageBuffer, 1000)
        .addPoolSize(::vk::DescriptorType::eCombinedImageSampler, 1000)
        .addPoolSize(::vk::DescriptorType::eStorageImage, 1000)
        .addPoolSize(::vk::DescriptorType::eSampler, 1000);
    // A pool size for a descriptor type Vulkan does not know about (because its extension was
    // never enabled — see Device::supportsRayTracing) is itself a validation error, not just a
    // wasted reservation.
    if (_device->supportsRayTracing()) {
        descriptorPoolBuilder.addPoolSize(::vk::DescriptorType::eAccelerationStructureKHR, 1000);
    }
    _descriptorPool = descriptorPoolBuilder.build();
}

void kor::vk::Context::Destroy()
{
    _device->operator*().waitIdle();
    delete _descriptorPool;
    delete _allocator;
    delete _device;
    delete _runtime;
}
