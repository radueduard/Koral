//
// Created by radue on 10/14/2024.
//

#pragma once

#include <functional>
#include <map>
#include <memory>
#include <glm/fwd.hpp>

#include <vulkan/vulkan.hpp>

#include "vk_wrapper.h"

namespace kor::vk {
    struct SubmitInfo;
    class Surface;
    class PhysicalDevice;
}

namespace kor::vk {
    class Queue final : public kor::vk::Wrapper<::vk::Queue> {
    public:
        class Family
        {
            friend class Queue;
        public:
            explicit Family(glm::u32 index, const ::vk::QueueFamilyProperties &properties);
            ~Family() = default;

            [[nodiscard]] glm::u32 getIndex() const { return _index; }
            [[nodiscard]] const ::vk::QueueFamilyProperties& getProperties() const { return _properties; }

            [[nodiscard]] std::unique_ptr<Queue> RequestQueue();
            [[nodiscard]] std::unique_ptr<Queue> RequestPresentQueue(const kor::vk::Surface& surface);

        private:
            glm::u32 _index;
            ::vk::QueueFamilyProperties _properties;
            glm::u32 _remainingQueues = 0;
        };

        explicit Queue(Family& family);
        ~Queue() override;

        Queue(const Queue &) = delete;
        Queue &operator=(const Queue &) = delete;

        [[nodiscard]] glm::u32 getIndex() const { return _index; }
        [[nodiscard]] const Family& getFamily() const { return _family; }
        [[nodiscard]] bool canPresent(const kor::vk::Surface& surface) const;
        [[nodiscard]] glm::u32 getIdentifier() const { return _identifier; }

        void Submit(const SubmitInfo& submitInfo) const;

    private:
        glm::u32 _identifier;
        glm::u32 _index;
        Family& _family;
    };

    class CommandBuffer;

    class Device final : public kor::vk::Wrapper<::vk::Device> {
    public:
        explicit Device();
        ~Device() override;

        void queuesWaitIdle() const;

        Device(const Device &) = delete;
        Device &operator=(const Device &) = delete;

        [[nodiscard]] const Queue& requestQueue(::vk::QueueFlags type) const;
        [[nodiscard]] const Queue& requestPresentQueue(const kor::vk::Surface& surface) const;
        void freeQueues() const;

        [[nodiscard]] std::unique_ptr<kor::vk::CommandBuffer> requestCommandBuffer(const kor::vk::Queue& queue, uint32_t thread) const;
        void freeCommandBuffer(const kor::vk::CommandBuffer &commandBuffer) const;

        void runSingleTimeCommand(const std::function<void(kor::vk::CommandBuffer&)> &command, ::vk::QueueFlags requiredFlags,
            ::vk::Fence fence = nullptr, ::vk::Semaphore waitSemaphore = nullptr, ::vk::Semaphore signalSemaphore = nullptr, bool wait = true) const;

        // Whether the physical device this Device was created on actually supports ray tracing
        // (acceleration structures + the ray tracing pipeline) — not every GPU does. Resources that
        // depend on it (RayTracingPipeline, AccelerationStructure) check this before calling into
        // functions that would otherwise be null (the loader never resolves them for a device the
        // extension was not enabled on).
        [[nodiscard]] bool supportsRayTracing() const { return _supportsRayTracing; }

    private:
        mutable std::vector<Queue::Family> _queueFamilies {};
        mutable std::vector<std::unique_ptr<Queue>> _queuesInUse {};
        mutable std::map<glm::u32, ::vk::CommandPool> _commandPools {};
        bool _supportsRayTracing = false;
    };
}
