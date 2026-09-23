//
// Created by radue on 10/14/2024.
//

#pragma once

#include "token.h"

#include <map>
#include <mutex>
#include <vector>

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

        // vkDeviceWaitIdle, under the queue lock: it touches every queue, which Vulkan requires
        // to be externally synchronised against a submit on another thread.
        void waitIdle() const;

        // Every vkQueue* call — submit, present, wait-idle — must hold this. Queues are shared
        // between threads (a one-off from a background coroutine and the frame on the main one
        // land on the same VkQueue), and Vulkan leaves synchronising them to the application.
        [[nodiscard]] std::unique_lock<std::mutex> lockQueues() const { return std::unique_lock(_queueMutex); }

        Device(const Device &) = delete;
        Device &operator=(const Device &) = delete;

        [[nodiscard]] const Queue& requestQueue(::vk::QueueFlags type) const;
        [[nodiscard]] const Queue& requestPresentQueue(const kor::vk::Surface& surface) const;
        void freeQueues() const;

        // A command buffer with a pool of its own, from a free list. A pool may only be used by one
        // thread at a time — recording into any buffer allocated from it included — and a
        // coroutine can record on one pool thread and finish on another, so a pool per *thread*
        // would still be shared. A pool per *command buffer* is used by whichever single thread is
        // recording that buffer, which is the one rule callers already follow.
        [[nodiscard]] std::unique_ptr<kor::vk::CommandBuffer> requestCommandBuffer(const kor::vk::Queue& queue) const;
        // Resets the buffer's pool and puts the pair back on the free list. The GPU must be done
        // with the buffer, as it had to be for vkFreeCommandBuffers before.
        void freeCommandBuffer(const kor::vk::CommandBuffer &commandBuffer) const;

        // Records `command` into a fresh command buffer on a queue with `requiredFlags` and submits
        // it, without waiting. The returned token is signalled when the GPU is done; the command
        // buffer is kept alive until then.
        [[nodiscard]] kor::Token runSingleTimeCommand(const std::function<void(kor::vk::CommandBuffer&)> &command, ::vk::QueueFlags requiredFlags) const;

        // Whether the physical device this Device was created on actually supports ray tracing
        // (acceleration structures + the ray tracing pipeline) — not every GPU does. Resources that
        // depend on it (RayTracingPipeline, AccelerationStructure) check this before calling into
        // functions that would otherwise be null (the loader never resolves them for a device the
        // extension was not enabled on).
        [[nodiscard]] bool supportsRayTracing() const { return _supportsRayTracing; }

    private:
        mutable std::vector<Queue::Family> _queueFamilies {};
        mutable std::vector<std::unique_ptr<Queue>> _queuesInUse {};
        mutable std::mutex _queuesMutex;   // guards the lazily filled _queuesInUse
        mutable std::mutex _queueMutex;    // see lockQueues()

        struct PooledCommandBuffer {
            ::vk::CommandPool pool;
            ::vk::CommandBuffer buffer;
        };
        mutable std::mutex _poolMutex;
        mutable std::map<glm::u32, std::vector<PooledCommandBuffer>> _freeCommandBuffers {}; // by queue identifier
        mutable std::vector<::vk::CommandPool> _commandPools {};                             // every pool ever made
        bool _supportsRayTracing = false;
    };
}
