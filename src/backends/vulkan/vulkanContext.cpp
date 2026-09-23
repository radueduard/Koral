//
// Created by radue on 2/27/2026.
//

#include "vulkanContext.h"

#include "allocator.h"
#include "descriptorPool.h"
#include "device.h"
#include "runtime.h"
#include "scheduler.h"
#include "timeline.h"
#include "../../core/tokenState.h"

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

kor::vk::TokenReactor& kor::vk::Context::Tokens()
{
    if (!_tokenReactor)
        throw std::runtime_error("TokenReactor is not initialized");
    return *_tokenReactor;
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
    _destroyImmediately = false;

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
        .addPoolSize(::vk::DescriptorType::eSampler, 1000)
        // Texel buffers — what a kor::BufferView binds as. Core Vulkan 1.0, so unlike the
        // acceleration structure below these need no capability check.
        .addPoolSize(::vk::DescriptorType::eUniformTexelBuffer, 1000)
        .addPoolSize(::vk::DescriptorType::eStorageTexelBuffer, 1000);
    // A pool size for a descriptor type Vulkan does not know about (because its extension was
    // never enabled — see Device::supportsRayTracing) is itself a validation error, not just a
    // wasted reservation.
    if (_device->supportsRayTracing()) {
        descriptorPoolBuilder.addPoolSize(::vk::DescriptorType::eAccelerationStructureKHR, 1000);
    }
    _descriptorPool = descriptorPoolBuilder.build();
    _tokenReactor = new kor::vk::TokenReactor;
}

void kor::vk::Context::StopTokens()
{
    if (_tokenReactor) _tokenReactor->shutdown();   // leaves the device idle
    // Everything deferred so far can go now, and anything destroyed from here on goes at once.
    kor::detail::collectRetired(/*all=*/true);
    _destroyImmediately = true;
}

void kor::vk::Context::DestroyWhenUnused(std::function<void()> destroy)
{
    if (_destroyImmediately || !_device) {
        destroy();
        return;
    }
    // A shared_ptr whose deleter is the destruction: dropped by the retire list once every
    // queue's last submitted epoch has been reached, or right here if they all have been.
    kor::detail::retireAfter(_device->submittedSoFar(),
        std::shared_ptr<void>(nullptr, [destroy = std::move(destroy)](void*) { destroy(); }));
}

void kor::vk::Context::Destroy()
{
    _device->waitIdle();
    kor::detail::collectRetired(/*all=*/true); // in case StopTokens() was never reached
    delete _tokenReactor;
    _tokenReactor = nullptr;
    delete _descriptorPool;
    delete _allocator;
    delete _device;
    delete _runtime;
}
