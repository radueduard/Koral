//
// Created by radue on 2/27/2026.
//

#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#define VMA_IMPLEMENTATION
#define VK_ENABLE_BETA_EXTENSIONS
#include "device.h"

#include <iostream>
#include <ranges>
#include <thread>
#include <unordered_map>

#include "commandBuffer.h"
#include "context.h"
#include "physicalDevice.h"
#include "runtime.h"
#include "scheduler.h"
#include "surface.h"
#include "vulkanContext.h"

namespace kor::vk {
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

    std::unique_ptr<Queue> Queue::Family::RequestPresentQueue(const kor::vk::Surface &surface) {
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
        _identifier = _family.getIndex() << 16 | _index;
        _handle = kor::vk::Context::Device()->getQueue(_family.getIndex(), _index);
    }

    Queue::~Queue() {
        _family._remainingQueues++;
    }

    bool Queue::canPresent(const kor::vk::Surface &surface) const {
        const auto& physicalDevice = Context::Runtime().getPhysicalDevice();
        return physicalDevice->getSurfaceSupportKHR(_family.getIndex(), *surface);
    }

    void Queue::Submit(const SubmitInfo& submitInfo) const
    {
        const auto commandBufferHandle = *submitInfo.commandBuffer;
        const auto commandBuffers = std::array { commandBufferHandle };
        auto submitInfoVulkan = ::vk::SubmitInfo()
            .setCommandBuffers(commandBuffers);

        if (!submitInfo.waitSemaphores.empty()) {
            submitInfoVulkan
                .setWaitSemaphores(submitInfo.waitSemaphores)
                .setWaitDstStageMask(submitInfo.waitStages);
        }

        if (!submitInfo.signalSemaphores.empty())
            submitInfoVulkan.setSignalSemaphores(submitInfo.signalSemaphores);

        try {
            _handle.submit(submitInfoVulkan, submitInfo.fence);
        } catch (const std::runtime_error& e) {
            std::cerr << e.what() << std::endl;
        }
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

#ifdef __APPLE__
        auto portabilityFeatures = ::vk::PhysicalDevicePortabilitySubsetFeaturesKHR()
            .setPNext(nullptr)
            .setTriangleFans(true)
            .setImageViewFormatSwizzle(true);
#endif

        auto deviceMeshShaderFeatures = ::vk::PhysicalDeviceMeshShaderFeaturesEXT()
#ifdef __APPLE__
            .setPNext(&portabilityFeatures)
#endif
            .setTaskShader(true)
            .setMeshShader(true);

        auto vk11Features = ::vk::PhysicalDeviceVulkan11Features()
            .setShaderDrawParameters(true)
            .setStorageBuffer16BitAccess(true)
            .setPNext(&deviceMeshShaderFeatures);

    	auto vk12Features = ::vk::PhysicalDeviceVulkan12Features()
			.setShaderInt8(true)
    		.setRuntimeDescriptorArray(true)
    		.setTimelineSemaphore(true)
            .setBufferDeviceAddress(true)
            .setVulkanMemoryModel(true)
            .setVulkanMemoryModelDeviceScope(true)
            .setScalarBlockLayout(true)
            .setStorageBuffer8BitAccess(true)
            .setShaderSampledImageArrayNonUniformIndexing(true)
            .setDescriptorBindingSampledImageUpdateAfterBind(true)
            .setDescriptorBindingPartiallyBound(true)
            .setDescriptorBindingVariableDescriptorCount(true)
            .setDescriptorIndexing(true)
			.setPNext(&vk11Features);

        auto vk13Features = ::vk::PhysicalDeviceVulkan13Features()
            .setShaderDemoteToHelperInvocation(true)
            .setDynamicRendering(true)
            .setPNext(&vk12Features);

        auto vk14Features = ::vk::PhysicalDeviceVulkan14Features()
            .setPNext(&vk13Features)
            .setIndexTypeUint8(true);

        auto maintenance9 = ::vk::PhysicalDeviceMaintenance9FeaturesKHR()
            .setPNext(&vk14Features)
            .setMaintenance9(true);

        auto accelerationStructureFeatures = ::vk::PhysicalDeviceAccelerationStructureFeaturesKHR()
            .setPNext(&maintenance9)
            .setAccelerationStructure(true);

        auto rayTracingPipelineFeatures = ::vk::PhysicalDeviceRayTracingPipelineFeaturesKHR()
            .setPNext(&accelerationStructureFeatures)
            .setRayTracingPipeline(true);

        auto deviceExtensions = Context::Runtime().getDeviceExtensions();

        auto features = Context::Runtime().getPhysicalDevice().getFeatures();

        const auto deviceCreateInfo = ::vk::DeviceCreateInfo()
            .setQueueCreateInfos(queueCreateInfos)
            .setPEnabledFeatures(&features)
            .setPNext(&rayTracingPipelineFeatures)
            .setPEnabledExtensionNames(deviceExtensions);

        _handle = physicalDevice->createDevice(deviceCreateInfo);
    	VULKAN_HPP_DEFAULT_DISPATCHER.init(_handle);
    }

    Device::~Device() {
        _handle.waitIdle();
        // Destroy the per-queue command pools before the device. In the windowed
        // path Scheduler::~Scheduler() already called freeQueues() (leaving
        // _queuesInUse empty, so this is a no-op); the headless path has no
        // Scheduler, so without this the pools would leak past vkDestroyDevice.
        freeQueues();
        _handle.destroy();
    }

    void Device::queuesWaitIdle() const {
        for (const auto& queue : _queuesInUse)
        {
            queue->operator*().waitIdle();
        }
    }

    const Queue& Device::requestQueue(const ::vk::QueueFlags type) const {
        for (const auto& queue : _queuesInUse)
        {
            if ((queue->getFamily().getProperties().queueFlags & type) == type)
            {
                return *queue;
            }
        }
        for (auto& queueFamily : _queueFamilies) {
            if ((queueFamily.getProperties().queueFlags & type) != type) {
                continue;
            }
            try {
                auto queue = queueFamily.RequestQueue();
                auto* queueRef = queue.get();
                // Keep the queue alive: without storing it, the unique_ptr would be
                // destroyed here and *queueRef returned as a dangling reference (the
                // Queue holds the vk::Queue handle, so the next submit would segfault).
                // Masked in the windowed path because requestPresentQueue() populates
                // _queuesInUse first with a family that also handles Koral/compute/transfer.
                _queuesInUse.emplace_back(std::move(queue));
                _commandPools[queueRef->getIdentifier()] = _handle.createCommandPool(::vk::CommandPoolCreateInfo()
                    .setFlags(::vk::CommandPoolCreateFlagBits::eTransient | ::vk::CommandPoolCreateFlagBits::eResetCommandBuffer)
                    .setQueueFamilyIndex(queueFamily.getIndex()));
                return *queueRef;
            } catch (const std::runtime_error&) {}
        }
        throw std::runtime_error("Device::RequestQueue: Failed to find a queue with this type!");
    }

    const Queue& Device::requestPresentQueue(const kor::vk::Surface& surface) const {
        for (const auto& queue : _queuesInUse)
        {
            if ((queue->canPresent(surface))) {
                return *queue;
            }
        }
        for (auto& queueFamily : _queueFamilies) {
            try {
                auto queue = queueFamily.RequestPresentQueue(surface);
                auto* queueRef = queue.get();
                _queuesInUse.emplace_back(std::move(queue));
                _commandPools[queueRef->getIdentifier()] = _handle.createCommandPool(::vk::CommandPoolCreateInfo()
                    .setFlags(::vk::CommandPoolCreateFlagBits::eTransient | ::vk::CommandPoolCreateFlagBits::eResetCommandBuffer)
                    .setQueueFamilyIndex(queueFamily.getIndex()));
                return *queueRef;
            } catch (const std::runtime_error& err) {
                std::cerr << err.what() << std::endl;
            }
        }
        throw std::runtime_error("Device::RequestPresentQueue: Failed to find suitable present queue!");
    }

    void Device::freeQueues() const {
        for (const auto& queue : _queuesInUse)
        {
            _handle.destroyCommandPool(_commandPools.at(queue->getIdentifier()));
        }
         _queuesInUse.clear();
    }

    std::unique_ptr<CommandBuffer> Device::requestCommandBuffer(const kor::vk::Queue& queue, const glm::u32 thread) const {
        const auto& commandPool = _commandPools.at(queue.getIdentifier());
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

    void Device::runSingleTimeCommand(const std::function<void(kor::vk::CommandBuffer&)> &command, const ::vk::QueueFlags requiredFlags,
        const ::vk::Fence fence, ::vk::Semaphore waitSemaphore, ::vk::Semaphore signalSemaphore, const bool wait) const
    {
        const auto& queue = requestQueue(requiredFlags);
        const auto commandBufferHolder = requestCommandBuffer(queue, std::hash<std::thread::id>{}(std::this_thread::get_id()));
        auto& commandBuffer = *commandBufferHolder.get();

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

