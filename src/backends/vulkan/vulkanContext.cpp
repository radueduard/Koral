//
// Created by radue on 2/27/2026.
//

module;

#include <stdexcept>
#include <vulkan/vulkan.hpp>

module vk.context;
import vk.runtime;
import vk.allocator;
import vk.device;
import vk.descriptorPool;

const gfx::vk::Runtime& gfx::vk::Context::Runtime()
{
    if (!_runtime)
        throw std::runtime_error("Runtime is not initialized");
    return *_runtime;
}

const gfx::vk::Device& gfx::vk::Context::Device()
{
    if (!_device)
        throw std::runtime_error("Device is not initialized");
    return *_device;
}

const gfx::vk::Allocator& gfx::vk::Context::Allocator()
{
    if (!_allocator)
        throw std::runtime_error("Allocator is not initialized");
    return *_allocator;
}

const gfx::vk::DescriptorPool& gfx::vk::Context::DescriptorPool()
{
    if (!_descriptorPool)
        throw std::runtime_error("DescriptorPool is not initialized");
    return *_descriptorPool;
}

void initDispatcher();

void gfx::vk::Context::Init()
{
    initDispatcher();

    _runtime = new gfx::vk::Runtime;
    _runtime->selectPhysicalDevice();
    _device = new gfx::vk::Device;
    _allocator = new gfx::vk::Allocator;
    _descriptorPool = gfx::vk::DescriptorPool::Builder()
        .setMaxSets(1000)
        .setPoolFlags(::vk::DescriptorPoolCreateFlagBits::eUpdateAfterBind | ::vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet)
        .addPoolSize(::vk::DescriptorType::eUniformBuffer, 1000)
        // Bindless texture arrays reserve up to 256 each (see DescriptorPool::
        // Allocate); the forward pass alone has 4 such sets, so keep ample room.
        .addPoolSize(::vk::DescriptorType::eSampledImage, 8192)
        .addPoolSize(::vk::DescriptorType::eStorageBuffer, 1000)
        .addPoolSize(::vk::DescriptorType::eCombinedImageSampler, 1000)
        .addPoolSize(::vk::DescriptorType::eStorageImage, 1000)
        .addPoolSize(::vk::DescriptorType::eSampler, 1000)
        .build();
}

void gfx::vk::Context::Destroy()
{
    _device->operator*().waitIdle();
    delete _descriptorPool;
    delete _allocator;
    delete _device;
    delete _runtime;
}
