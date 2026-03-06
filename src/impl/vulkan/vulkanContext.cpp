//
// Created by radue on 2/27/2026.
//

#include "vulkanContext.h"

#include "allocator.h"
#include "device.h"
#include "runtime.h"

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

void gfx::vk::Context::Init()
{
    _threadId = std::this_thread::get_id();
    _runtime = new gfx::vk::Runtime;
    _runtime->selectPhysicalDevice();
    _device = new gfx::vk::Device;
    _allocator = new gfx::vk::Allocator;
}

void gfx::vk::Context::Destroy()
{
    delete _device;
    delete _runtime;
}
