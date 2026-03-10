//
// Created by radue on 2/27/2026.
//

#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#define VMA_IMPLEMENTATION
#include "device.h"

#include <iostream>
#include <ranges>
#include <thread>
#include <unordered_map>

#include "commandBuffer.h"
#include "context.h"
#include "physicalDevice.h"
#include "runtime.h"
#include "surface.h"
#include "vulkanContext.h"

namespace gfx::vk {
    Queue::Family::Family(const glm::u32 index, const ::vk::QueueFamilyProperties &properties) :
        _index(index),
        _properties(properties) {
        _remainingQueues = properties.queueCount;
    }

    std::unique_ptr<Queue> Queue::Family::RequestQueue() {
        try {
            return std::make_unique<Queue>(*this);
        } catch (const std::runtime_error& err) {
            throw std::runtime_error("QueueFamily::RequestQueue : \n" + std::string(err.what()));
        }
    }

    std::unique_ptr<Queue> Queue::Family::RequestPresentQueue(const gfx::vk::Surface &surface) {
        const auto& physicalDevice = Context::Runtime().getPhysicalDevice();

        if (physicalDevice->getSurfaceSupportKHR(_index, *surface)) {
            return std::make_unique<Queue>(*this);
        }
        throw std::runtime_error("QueueFamily::RequestPresentQueue : Queue family cannot present");
    }

    Queue::Queue(Family &family): _family(family) {
        if (_family._remainingQueues == 0) {
            throw std::runtime_error("Queue::Queue : No more queues available in this family");
        }
        _index = _family._properties.queueCount - _family._remainingQueues--;
        _handle = gfx::vk::Context::Device()->getQueue(_family.getIndex(), _index);
    }

    Queue::~Queue() {
        _family._remainingQueues++;
    }

    Device::Device() {
        const auto& physicalDevice = Context::Runtime().getPhysicalDevice();
        for (const auto& queueFamily : physicalDevice.getQueueFamilyProperties()) {
            const auto queueFamilyIndex = static_cast<uint32_t>(&queueFamily - physicalDevice.getQueueFamilyProperties().data());

            _queueFamilies.emplace_back(queueFamilyIndex, queueFamily);
        }

        std::vector<::vk::DeviceQueueCreateInfo> queueCreateInfos;
        std::vector<std::vector<float>> queuePriorities;
        for (const auto& queueFamily : _queueFamilies) {
            queuePriorities.emplace_back(queueFamily.getProperties().queueCount, 1.0f);
            const auto queueCreateInfo = ::vk::DeviceQueueCreateInfo()
                .setQueueFamilyIndex(queueFamily.getIndex())
                .setQueuePriorities(queuePriorities.back());
            queueCreateInfos.emplace_back(queueCreateInfo);
        }

        auto deviceMeshShaderFeatures = ::vk::PhysicalDeviceMeshShaderFeaturesEXT()
            .setTaskShader(true)
            .setMeshShader(true);

     //    auto maintenance4Features = vk::PhysicalDeviceMaintenance4Features()
     //        .setMaintenance4(true)
     //        .setPNext(&deviceMeshShaderFeatures);

    	auto dynamicRenderingFeatures = ::vk::PhysicalDeviceDynamicRenderingFeatures()
			.setDynamicRendering(true)
			.setPNext(&deviceMeshShaderFeatures);

        auto vk11Features = ::vk::PhysicalDeviceVulkan11Features()
            .setShaderDrawParameters(true)
            .setPNext(&dynamicRenderingFeatures);

    	auto vk12Features = ::vk::PhysicalDeviceVulkan12Features()
			.setShaderInt8(true)
    		.setRuntimeDescriptorArray(true)
    		.setTimelineSemaphore(true)
            .setBufferDeviceAddress(true)
			.setPNext(&vk11Features);

    	// auto maintenance4Features = vk::PhysicalDeviceMaintenance4Features()
    	// 	.setMaintenance4(true)
			// .setPNext(&vk12Features);

   //  	auto vk13Features = vk::PhysicalDeviceVulkan13Features()
			// .setShaderDemoteToHelperInvocation(true)
			// .setPNext(&maintenance4Features);

        auto deviceExtensions = Context::Runtime().getDeviceExtensions();

        const auto deviceCreateInfo = ::vk::DeviceCreateInfo()
            .setQueueCreateInfos(queueCreateInfos)
            .setPEnabledFeatures(&Context::Runtime().getPhysicalDevice().getFeatures())
            .setPNext(&vk12Features)
            .setPEnabledExtensionNames(deviceExtensions);

        _handle = physicalDevice->createDevice(deviceCreateInfo);
    	VULKAN_HPP_DEFAULT_DISPATCHER.init(_handle);

        for (const auto& queueFamily : _queueFamilies) {
            _commandPools[queueFamily.getIndex()] = {};
        }
    }

    Device::~Device() {
        _handle.waitIdle();
        _handle.destroy();
    }

    void Device::createCommandPools(const uint32_t threadId) const {
        for (const auto& queueFamily : _queueFamilies) {
            const auto commandPoolCreateInfo = ::vk::CommandPoolCreateInfo()
                .setFlags(::vk::CommandPoolCreateFlagBits::eTransient | ::vk::CommandPoolCreateFlagBits::eResetCommandBuffer)
                .setQueueFamilyIndex(queueFamily.getIndex());
            _commandPools[queueFamily.getIndex()].emplace(threadId, _handle.createCommandPool(commandPoolCreateInfo));
        }
    }

    void Device::freeCommandPools(const uint32_t threadId) const {
        for (const auto& queueFamily : _queueFamilies) {
            _handle.destroyCommandPool(_commandPools[queueFamily.getIndex()][threadId]);
            _commandPools[queueFamily.getIndex()].erase(threadId);
        }
    }

    const Queue& Device::requestQueue(const ::vk::QueueFlags type) const {
        // for (const auto& queue : _queuesInUse)
        // {
        //     if ((queue->getFamily().getProperties().queueFlags & type) == type)
        //     {
        //         return *queue;
        //     }
        // }

        for (auto& queueFamily : _queueFamilies) {
            if ((queueFamily.getProperties().queueFlags & type) != type) {
                continue;
            }
            try {
                auto queue = queueFamily.RequestQueue();
                const auto& queueRef =_queuesInUse.emplace_back(std::move(queue));
                return *queueRef;
            } catch (const std::runtime_error&) {}
        }
        throw std::runtime_error("Queue::RequestQueue : Failed to find queue with requested flags");
    }

    const Queue& Device::requestPresentQueue(const gfx::vk::Surface& surface) const {
        for (auto& queueFamily : _queueFamilies) {
            try {
                auto queue = queueFamily.RequestPresentQueue(surface);
                const auto& queueRef = _queuesInUse.emplace_back(std::move(queue));
                return *queueRef;
            } catch (const std::runtime_error&) {
                continue;
            }
        }
        throw std::runtime_error("Device::RequestPresentQueue: Failed to find suitable present queue!");
    }

    std::unique_ptr<CommandBuffer> Device::requestCommandBuffer(const gfx::vk::Queue& queue, const glm::u32 thread) const {
        const auto& commandPool = _commandPools.at(queue.getFamily().getIndex()).at(thread);
        const auto commandBufferAllocInfo = ::vk::CommandBufferAllocateInfo()
            .setCommandPool(commandPool)
            .setLevel(::vk::CommandBufferLevel::ePrimary)
            .setCommandBufferCount(1);
        const auto commandBuffers = _handle.allocateCommandBuffers(commandBufferAllocInfo);
        return std::make_unique<CommandBuffer>(queue, commandBuffers.front(), commandPool);
    }

    void Device::freeCommandBuffer(const CommandBuffer &commandBuffer) const {
        _handle.freeCommandBuffers(commandBuffer.getParentPool(), *commandBuffer);
    }

    void Device::runSingleTimeCommand(const std::function<void(const gfx::vk::CommandBuffer&)> &command, const ::vk::QueueFlags requiredFlags,
        const ::vk::Fence fence, ::vk::Semaphore waitSemaphore, ::vk::Semaphore signalSemaphore, const bool wait) const
    {
        glm::u32 thread = 0;
        if (Context::ThreadId() != Context::MainThreadId()) {
            thread = Context::ThreadId();
        }

        const auto queue = requestQueue(requiredFlags);
        const auto commandBufferHolder = requestCommandBuffer(queue, thread);
        const auto& commandBuffer = *commandBufferHolder.get();

        commandBuffer->begin(::vk::CommandBufferBeginInfo().setFlags(::vk::CommandBufferUsageFlagBits::eOneTimeSubmit));
        command(commandBuffer);
        commandBuffer->end();

        const auto commandBuffers = std::array { *commandBuffer };
        const auto dstStageMask = std::vector<::vk::PipelineStageFlags> { ::vk::PipelineStageFlagBits::eAllCommands };
        auto submitInfo = ::vk::SubmitInfo()
            .setCommandBuffers(commandBuffers);

        if (waitSemaphore != nullptr)
            submitInfo
                .setWaitSemaphores(waitSemaphore)
                .setWaitDstStageMask(dstStageMask);

        if (signalSemaphore != nullptr)
            submitInfo.setSignalSemaphores(signalSemaphore);

        try {
            queue->submit(submitInfo, fence);
        } catch (const std::runtime_error& e) {
            std::cerr << e.what() << std::endl;
        }
    	if (wait) {
			queue->waitIdle();
		}
    }
}

