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

        // Feature negotiation. vkCreateDevice does not degrade gracefully: request a single
        // feature the device lacks and the whole call fails with VK_ERROR_FEATURE_NOT_PRESENT,
        // taking the engine down on hardware that could otherwise have run it. This used to ask
        // unconditionally for mesh shaders (Turing and later), Vulkan 1.4 and maintenance9, which
        // turned away everything older than an RTX 20-series card — including for features the
        // engine never actually used. So: nothing is requested here that is not either genuinely
        // required or confirmed present.
        //
        // Deliberately no longer requested, because nothing in the engine or the shaders uses
        // them and each one silently narrowed the supported hardware: maintenance9,
        // indexTypeUint8 (and with it the whole Vulkan 1.4 requirement), vulkanMemoryModel,
        // storageBuffer8BitAccess/storageBuffer16BitAccess and shaderInt8.
        const auto supportedFeatures = physicalDevice->template getFeatures2<
            ::vk::PhysicalDeviceFeatures2,
            ::vk::PhysicalDeviceVulkan11Features,
            ::vk::PhysicalDeviceVulkan12Features,
            ::vk::PhysicalDeviceVulkan13Features>();

        const auto& supported11 = supportedFeatures.get<::vk::PhysicalDeviceVulkan11Features>();
        const auto& supported12 = supportedFeatures.get<::vk::PhysicalDeviceVulkan12Features>();
        const auto& supported13 = supportedFeatures.get<::vk::PhysicalDeviceVulkan13Features>();

        // Collects anything missing so device creation fails naming the feature, rather than with
        // an opaque VK_ERROR_FEATURE_NOT_PRESENT that says nothing about which one was at fault.
        std::vector<std::string_view> missingFeatures;
        const auto require = [&missingFeatures](const ::vk::Bool32 supported, const std::string_view name) {
            if (!supported) missingFeatures.emplace_back(name);
            return supported;
        };

        // Mesh/task shaders are an opt-in pipeline option (GraphicsPipeline::Builder::setMeshShader),
        // never used by a default render path — so they are gated on the extension rather than
        // required. Note the feature struct is only legal to chain in when the extension itself is
        // enabled, which it previously was not.
        const bool meshShaderSupported =
            physicalDevice.supportsExtension(VK_EXT_MESH_SHADER_EXTENSION_NAME);

        auto deviceMeshShaderFeatures = ::vk::PhysicalDeviceMeshShaderFeaturesEXT()
#ifdef __APPLE__
            .setPNext(&portabilityFeatures)
#endif
            .setTaskShader(true)
            .setMeshShader(true);

        auto vk13Features = ::vk::PhysicalDeviceVulkan13Features()
            .setShaderDemoteToHelperInvocation(require(supported13.shaderDemoteToHelperInvocation, "shaderDemoteToHelperInvocation"))
            .setDynamicRendering(require(supported13.dynamicRendering, "dynamicRendering"));

        if (meshShaderSupported) {
            vk13Features.setPNext(&deviceMeshShaderFeatures);
        }
#ifdef __APPLE__
        else {
            vk13Features.setPNext(&portabilityFeatures);
        }
#endif

    	auto vk12Features = ::vk::PhysicalDeviceVulkan12Features()
    		.setRuntimeDescriptorArray(require(supported12.runtimeDescriptorArray, "runtimeDescriptorArray"))
    		.setTimelineSemaphore(require(supported12.timelineSemaphore, "timelineSemaphore"))
            .setBufferDeviceAddress(require(supported12.bufferDeviceAddress, "bufferDeviceAddress"))
            .setScalarBlockLayout(require(supported12.scalarBlockLayout, "scalarBlockLayout"))
            .setShaderSampledImageArrayNonUniformIndexing(require(supported12.shaderSampledImageArrayNonUniformIndexing, "shaderSampledImageArrayNonUniformIndexing"))
            .setDescriptorBindingSampledImageUpdateAfterBind(require(supported12.descriptorBindingSampledImageUpdateAfterBind, "descriptorBindingSampledImageUpdateAfterBind"))
            .setDescriptorBindingPartiallyBound(require(supported12.descriptorBindingPartiallyBound, "descriptorBindingPartiallyBound"))
            .setDescriptorBindingVariableDescriptorCount(require(supported12.descriptorBindingVariableDescriptorCount, "descriptorBindingVariableDescriptorCount"))
            .setDescriptorIndexing(require(supported12.descriptorIndexing, "descriptorIndexing"))
			.setPNext(&vk13Features);

        auto vk11Features = ::vk::PhysicalDeviceVulkan11Features()
            .setShaderDrawParameters(require(supported11.shaderDrawParameters, "shaderDrawParameters"))
            .setPNext(&vk12Features);

        if (!missingFeatures.empty()) {
            std::string names;
            for (const auto& name : missingFeatures) {
                if (!names.empty()) names += ", ";
                names += name;
            }
            throw std::runtime_error(
                "Device::Device : GPU '" + std::string(physicalDevice.getProperties().deviceName.data()) +
                "' is missing required Vulkan features: " + names);
        }

        auto accelerationStructureFeatures = ::vk::PhysicalDeviceAccelerationStructureFeaturesKHR()
            .setPNext(&vk11Features)
            .setAccelerationStructure(true);

        auto rayTracingPipelineFeatures = ::vk::PhysicalDeviceRayTracingPipelineFeaturesKHR()
            .setPNext(&accelerationStructureFeatures)
            .setRayTracingPipeline(true);

        // Required extensions plus whichever optional ones (ray tracing, ...) this specific
        // physical device actually advertises — never assumed, since they are not universal.
        auto deviceExtensions = Context::Runtime().getRequiredDeviceExtensions();
        for (const auto* optional : Context::Runtime().getOptionalDeviceExtensions()) {
            if (physicalDevice.supportsExtension(optional)) {
                deviceExtensions.push_back(optional);
            }
        }

        _supportsRayTracing = physicalDevice.supportsExtension(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME)
            && physicalDevice.supportsExtension(VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME)
            && physicalDevice.supportsExtension(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);

        auto features = Context::Runtime().getPhysicalDevice().getFeatures();

        auto deviceCreateInfo = ::vk::DeviceCreateInfo()
            .setQueueCreateInfos(queueCreateInfos)
            .setPEnabledFeatures(&features)
            .setPEnabledExtensionNames(deviceExtensions);

        // The ray tracing feature structs must not be chained in at all unless the corresponding
        // extensions were just enabled above — each one is only meaningful (and, per spec, only
        // valid to pass) together with its extension.
        if (_supportsRayTracing) {
            deviceCreateInfo.setPNext(&rayTracingPipelineFeatures);
        } else {
            deviceCreateInfo.setPNext(&vk11Features);
        }

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

