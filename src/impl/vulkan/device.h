//
// Created by radue on 2/27/2026.
//

#pragma once


//
// Created by radue on 10/14/2024.
//

#pragma once

#include <functional>
#include <optional>
#include <unordered_map>
#include <glm/fwd.hpp>

#include <vulkan/vulkan.hpp>

#include <vma/vk_mem_alloc.h>

#include "utils/vk_wrapper.h"

namespace gfx::vk {
    class PhysicalDevice;
}

namespace gfx::vk {
    class Queue final : public gfx::vk::Wrapper<::vk::Queue> {
    public:
        class Family
        {
            friend class Queue;
        public:
            explicit Family(glm::u32 index, const ::vk::QueueFamilyProperties &properties, bool canPresent);
            ~Family() = default;

            [[nodiscard]] glm::u32 getIndex() const { return _index; }
            [[nodiscard]] const ::vk::QueueFamilyProperties& getProperties() const { return _properties; }
            [[nodiscard]] bool canPresent() const { return _canPresent; }

            [[nodiscard]] std::unique_ptr<Queue> RequestQueue();
            [[nodiscard]] std::unique_ptr<Queue> RequestPresentQueue();

        private:
            glm::u32 _index;
            ::vk::QueueFamilyProperties _properties;
            glm::u32 _remainingQueues = 0;
            bool _canPresent = false;
        };

        explicit Queue(Family& family);
        ~Queue() override;

        [[nodiscard]] glm::u32 getIndex() const { return _index; }
        [[nodiscard]] const Family& getFamily() const { return _family; }

    private:
        glm::u32 _index;
        Family& _family;
    };

    class CommandBuffer;

    class Device final : public gfx::vk::Wrapper<::vk::Device> {
    public:
        explicit Device();
        ~Device() override;

        Device(const Device &) = delete;
        Device &operator=(const Device &) = delete;

        [[nodiscard]] const Queue& requestQueue(::vk::QueueFlags type) const;
        [[nodiscard]] const Queue& requestPresentQueue() const;

        void createCommandPools(uint32_t threadId);
        void freeCommandPools(uint32_t threadId);

        [[nodiscard]] std::unique_ptr<gfx::vk::CommandBuffer> requestCommandBuffer(const gfx::vk::Queue& queue, uint32_t thread = 0) const;
        void freeCommandBuffer(const gfx::vk::CommandBuffer &commandBuffer) const;

        void runSingleTimeCommand(const std::function<void(const gfx::vk::CommandBuffer&)> &command, ::vk::QueueFlags requiredFlags,
            ::vk::Fence fence = nullptr, ::vk::Semaphore waitSemaphore = nullptr, ::vk::Semaphore signalSemaphore = nullptr, bool wait = true) const;

    private:
    	std::unique_ptr<VmaAllocator> _allocator;
        mutable std::vector<Queue::Family> _queueFamilies {};
        mutable std::vector<std::unique_ptr<Queue>> _queuesInUse {};
        std::unordered_map<uint32_t, std::unordered_map<uint32_t, ::vk::CommandPool>> _commandPools;
    };
}
