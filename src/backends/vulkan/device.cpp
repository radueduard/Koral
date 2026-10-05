//
// Created by radue on 2/27/2026.
//

#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#define VMA_IMPLEMENTATION
#define VK_ENABLE_BETA_EXTENSIONS
#include "device.h"
#include "log.h"
#include "../../core/tokenState.h"
#include "timeline.h"

#include <cstdlib>
#include <iostream>
#include <string_view>
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
        // Always chained, and always with a value per semaphore: once any semaphore in the batch is
        // a timeline, Vulkan wants the value arrays to line up with the semaphore arrays one for one,
        // binary semaphores included (their values are ignored).
        try {
            const auto lock = Context::Device().lockQueues();
            auto signalSemaphores = submitInfo.signalSemaphores;
            auto signalValues = submitInfo.signalValues;
            const auto [epochSemaphore, epochValue] = Context::Device().nextEpoch(*this);
            signalSemaphores.push_back(epochSemaphore);
            signalValues.push_back(epochValue);

            auto timelineInfo = ::vk::TimelineSemaphoreSubmitInfo()
                .setWaitSemaphoreValues(submitInfo.waitValues)
                .setSignalSemaphoreValues(signalValues);
            const auto submitInfoVulkan = ::vk::SubmitInfo()
                .setCommandBuffers(submitInfo.commandBuffers)
                .setWaitSemaphores(submitInfo.waitSemaphores)
                .setWaitDstStageMask(submitInfo.waitStages)
                .setSignalSemaphores(signalSemaphores)
                .setPNext(&timelineInfo);
            try {
                _handle.submit(submitInfoVulkan, submitInfo.fence);
            } catch (...) {
                Context::Device().abandonEpoch(*this);
                throw;
            }
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

        // Mesh/task shaders are an opt-in pipeline option (GraphicsPipeline::Builder::SetMeshShader),
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

        // A present mode that shows the newest finished frame at each refresh and never makes the
        // application wait for one: what "vsync" is asked of where there is no mailbox (NVIDIA on X11),
        // and waiting for the display (FIFO) costs frames it should not. Enabled where the device has it.
        const char* fifoLatestReady =
            physicalDevice.supportsExtension(VK_KHR_PRESENT_MODE_FIFO_LATEST_READY_EXTENSION_NAME) ? VK_KHR_PRESENT_MODE_FIFO_LATEST_READY_EXTENSION_NAME
            : physicalDevice.supportsExtension(VK_EXT_PRESENT_MODE_FIFO_LATEST_READY_EXTENSION_NAME) ? VK_EXT_PRESENT_MODE_FIFO_LATEST_READY_EXTENSION_NAME
            : nullptr;
        auto fifoLatestReadyFeatures = ::vk::PhysicalDevicePresentModeFifoLatestReadyFeaturesKHR().setPresentModeFifoLatestReady(true);
        if (fifoLatestReady) {
            fifoLatestReadyFeatures.setPNext(const_cast<void*>(static_cast<const void*>(vk13Features.pNext)));
            vk13Features.setPNext(&fifoLatestReadyFeatures);
        }
        _supportsFifoLatestReady = fifoLatestReady != nullptr;

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
        if (fifoLatestReady) deviceExtensions.push_back(fifoLatestReady);

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
        std::lock_guard queues(_queuesMutex);
        const auto lock = lockQueues();
        for (const auto& queue : _queuesInUse)
        {
            queue->operator*().waitIdle();
        }
        if (_asyncComputeQueue) (**_asyncComputeQueue).waitIdle();
    }

    std::pair<::vk::Semaphore, std::uint64_t> Device::nextEpoch(const Queue& queue) const {
        auto& epoch = _epochs[queue.getIdentifier()];
        const auto [semaphore, value] = Context::Tokens().resolve(epoch.timeline.At(epoch.submitted + 1));
        ++epoch.submitted;
        return {semaphore, value};
    }

    std::vector<kor::Token> Device::submittedSoFar() const {
        const auto lock = lockQueues();
        std::vector<kor::Token> tokens;
        for (const auto& epoch : _epochs | std::views::values)
            if (epoch.submitted > 0) tokens.push_back(epoch.timeline.At(epoch.submitted));
        return tokens;
    }

    void Device::markEpoch(const Queue& queue) const {
        const auto lock = lockQueues();
        const auto [semaphore, value] = nextEpoch(queue);
        auto timelineInfo = ::vk::TimelineSemaphoreSubmitInfo().setSignalSemaphoreValues(value);
        try {
            queue->submit(::vk::SubmitInfo().setSignalSemaphores(semaphore).setPNext(&timelineInfo));
        } catch (...) {
            abandonEpoch(queue);
            throw;
        }
    }

    void Device::waitIdle() const {
        const auto lock = lockQueues();
        _handle.waitIdle();
    }

    const Queue& Device::requestQueue(const ::vk::QueueFlags type) const {
        std::lock_guard lock(_queuesMutex);
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
                return *queueRef;
            } catch (const std::runtime_error&) {}
        }
        throw std::runtime_error("Device::RequestQueue: Failed to find a queue with this type!");
    }

    const Queue& Device::requestAsyncComputeQueue() const {
        const Queue& frame = requestQueue(::vk::QueueFlagBits::eGraphics);
        std::lock_guard lock(_queuesMutex);
        if (!_asyncComputeChosen) {
            _asyncComputeChosen = true;
            // A second queue of the frame's family where there is one — nothing to share or transfer —
            // and a family of compute queues otherwise, which is what AMD and Intel offer instead.
            // KORAL_ASYNC_COMPUTE=separate-family takes the second even where the first exists, to
            // exercise that path on a device that would not.
            const char* forced = std::getenv("KORAL_ASYNC_COMPUTE");
            const bool separate = forced && std::string_view(forced) == "separate-family";
            if (!separate) {
                for (auto& family : _queueFamilies) {
                    if (family.getIndex() != frame.getFamily().getIndex()) continue;
                    try { _asyncComputeQueue = family.RequestQueue(); } catch (const std::runtime_error&) {}
                }
            }
            if (!_asyncComputeQueue) {
                for (auto& family : _queueFamilies) {
                    const auto flags = family.getProperties().queueFlags;
                    if (!(flags & ::vk::QueueFlagBits::eCompute) || (flags & ::vk::QueueFlagBits::eGraphics)) continue;
                    try { _asyncComputeQueue = family.RequestQueue(); break; } catch (const std::runtime_error&) {}
                }
            }
            // Neither: async compute runs on the frame's queue, in order.
        }
        return _asyncComputeQueue ? *_asyncComputeQueue : frame;
    }

    std::vector<glm::u32> Device::sharedFamilies() const {
        const Queue& async = requestAsyncComputeQueue();
        const Queue& frame = requestQueue(::vk::QueueFlagBits::eGraphics);
        if (async.getFamily().getIndex() == frame.getFamily().getIndex()) return {};
        return { frame.getFamily().getIndex(), async.getFamily().getIndex() };
    }

    const Queue& Device::requestPresentQueue(const kor::vk::Surface& surface) const {
        std::lock_guard lock(_queuesMutex);
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
                return *queueRef;
            } catch (const std::runtime_error& err) {
                std::cerr << err.what() << std::endl;
            }
        }
        throw std::runtime_error("Device::RequestPresentQueue: Failed to find suitable present queue!");
    }

    void Device::freeQueues() const {
        {
            // Every pool, free or still leased: a command buffer outliving this is already a bug,
            // and freeCommandBuffer() below notices the pool is gone rather than touch it.
            std::lock_guard lock(_poolMutex);
            for (const auto pool : _commandPools) _handle.destroyCommandPool(pool);
            _commandPools.clear();
            for (const auto& free : _freeCommandBuffers | std::views::values) {
                for (const auto& recycled : free) {
                    _handle.destroyFence(recycled.fence);
                    if (recycled.timerPool) _handle.destroyQueryPool(recycled.timerPool);
                }
            }
            _freeCommandBuffers.clear();
        }
        std::lock_guard lock(_queuesMutex);
        _queuesInUse.clear();
        _asyncComputeQueue.reset();
        _asyncComputeChosen = false;
    }

    std::unique_ptr<CommandBuffer> Device::requestCommandBuffer(const kor::vk::Queue& queue) const {
        ::vk::CommandPool pool;
        {
            std::lock_guard lock(_poolMutex);
            if (auto& free = _freeCommandBuffers[queue.getIdentifier()]; !free.empty()) {
                const auto recycled = free.back();
                free.pop_back();
                return std::make_unique<CommandBuffer>(queue, recycled.buffer, recycled.pool, recycled.fence, recycled.timerPool);
            }
            pool = _handle.createCommandPool(::vk::CommandPoolCreateInfo()
                // Reset-per-buffer so re-recording a buffer resets it implicitly on begin.
                .setFlags(::vk::CommandPoolCreateFlagBits::eResetCommandBuffer)
                .setQueueFamilyIndex(queue.getFamily().getIndex()));
            _commandPools.push_back(pool);
        }
        // The pool is this buffer's alone from here on, so allocating needs no lock.
        const auto buffers = _handle.allocateCommandBuffers(::vk::CommandBufferAllocateInfo()
            .setCommandPool(pool)
            .setLevel(::vk::CommandBufferLevel::ePrimary)
            .setCommandBufferCount(1));
        return std::make_unique<CommandBuffer>(queue, buffers.front(), pool);
    }

    bool Device::freeCommandBuffer(const CommandBuffer &commandBuffer) const {
        const auto pool = commandBuffer.getParentPool();
        std::lock_guard lock(_poolMutex);
        if (std::ranges::find(_commandPools, pool) == _commandPools.end()) return false; // freeQueues() got there first
        // Still this buffer's alone until it is on the free list, but resetting under the lock
        // costs nothing that matters and keeps the "gone" check above honest.
        _handle.resetCommandPool(pool);
        // The next owner submits with this fence, which must be unsignalled by then.
        const auto fence = commandBuffer.getFence();
        if (_handle.getFenceStatus(fence) == ::vk::Result::eSuccess) (void)_handle.resetFences(1, &fence);
        _freeCommandBuffers[commandBuffer.getQueue().getIdentifier()].push_back(
            {pool, *commandBuffer, fence, commandBuffer.getTimerPool()});
        return true;
    }

    kor::Token Device::runSingleTimeCommand(const std::function<void(kor::vk::CommandBuffer&)> &command, const ::vk::QueueFlags requiredFlags) const
    {
        // Earlier one-offs the GPU has since finished.
        detail::collectRetired();

        const auto& queue = requestQueue(requiredFlags);
        std::shared_ptr<CommandBuffer> commandBuffer = requestCommandBuffer(queue);

        (*commandBuffer)->begin(::vk::CommandBufferBeginInfo().setFlags(::vk::CommandBufferUsageFlagBits::eOneTimeSubmit));
        command(*commandBuffer);
        (*commandBuffer)->end();

        const kor::Token done = kor::Token::Create();
        const auto [semaphore, value] = Context::Tokens().resolve(done);
        const auto commandBuffers = std::array { **commandBuffer };

        // After every upload not yet done, as a one-off submitted through kor::CommandBuffer is.
        std::vector<::vk::Semaphore> waitSemaphores;
        std::vector<std::uint64_t> waitValues;
        std::vector<::vk::PipelineStageFlags> waitStages;
        for (const auto& upload : detail::pendingUploads()) {
            const auto [waitSemaphore, waitValue] = Context::Tokens().resolve(upload);
            waitSemaphores.push_back(waitSemaphore);
            waitValues.push_back(waitValue);
            waitStages.push_back(::vk::PipelineStageFlagBits::eAllCommands);
        }

        try {
            {
                const auto lock = lockQueues();
                const auto [epochSemaphore, epochValue] = nextEpoch(queue);
                const auto semaphores = std::array { semaphore, epochSemaphore };
                const auto values = std::array { value, epochValue };
                auto timelineInfo = ::vk::TimelineSemaphoreSubmitInfo()
                    .setWaitSemaphoreValues(waitValues)
                    .setSignalSemaphoreValues(values);
                try {
                    queue->submit(::vk::SubmitInfo()
                        .setWaitSemaphores(waitSemaphores)
                        .setWaitDstStageMask(waitStages)
                        .setCommandBuffers(commandBuffers)
                        .setSignalSemaphores(semaphores)
                        .setPNext(&timelineInfo));
                } catch (...) {
                    abandonEpoch(queue);
                    throw;
                }
            }
            TokenReactor::noteSubmittedSignal(done);
        } catch (const std::exception& e) {
            kor::log::Error("[vulkan] single-time command failed to submit: {}", e.what());
            done.Signal(); // nothing on the GPU will; don't leave its waiters hanging
            return done;
        }
        detail::retireAfter(done, std::move(commandBuffer));
        return done;
    }
}

