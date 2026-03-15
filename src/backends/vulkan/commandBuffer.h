//
// Created by radue on 2/27/2026.
//

#pragma once

#include <functional>
#include "vk_wrapper.h"
#include <vulkan/vulkan.hpp>

#include <comandBuffer.h>

namespace gfx::vk
{
    class Queue;

    class CommandBuffer : public gfx::CommandBuffer, public gfx::vk::Wrapper<::vk::CommandBuffer> {
    public:
        CommandBuffer(const gfx::vk::Queue& queue, ::vk::CommandBuffer commandBuffer, const ::vk::CommandPool& parentCommandPool);
        ~CommandBuffer() override;
        void Run(const std::function<void(const gfx::vk::CommandBuffer&)>& command, ::vk::Semaphore waitSemaphore = nullptr) const;

        [[nodiscard]] const ::vk::CommandPool& getParentPool() const { return _parentPool; }
        [[nodiscard]] const ::vk::Semaphore& getSignalSemaphore() const { return _signalSemaphore; }
        [[nodiscard]] const ::vk::Fence& getFence() const { return _fence; }
        [[nodiscard]] const gfx::vk::Queue& getQueue() const { return _queue; }

        gfx::CommandBuffer& Begin() override;
        void End() override;
        gfx::CommandBuffer& BeginRendering(const gfx::Framebuffer* framebuffer) override;
        gfx::CommandBuffer& EndRendering() override;
        gfx::CommandBuffer& SetViewport(glm::u32 x, glm::u32 y, glm::u32 width, glm::u32 height) override;
        gfx::CommandBuffer& SetScissor(glm::u32 x, glm::u32 y, glm::u32 width, glm::u32 height) override;
        gfx::CommandBuffer& BindPipeline(const gfx::ComputePipeline* pipeline) override;
        gfx::CommandBuffer& BindPipeline(const gfx::GraphicsPipeline* pipeline) override;
        gfx::CommandBuffer& BindDescriptorSet(glm::u32 index, const gfx::DescriptorSet* set) override;
        gfx::CommandBuffer& Dispatch(glm::u32 groupCountX, glm::u32 groupCountY, glm::u32 groupCountZ) override;
        gfx::CommandBuffer& Draw(glm::u32 vertexCount, glm::u32 instanceCount, glm::u32 firstVertex, glm::u32 firstInstance) override;
        gfx::CommandBuffer& DrawMesh(const Mesh* mesh, glm::u32 instanceCount, glm::u32 baseInstance) override;
        gfx::CommandBuffer& Blit(const gfx::Image* srcImage, const gfx::Image* dstImage) override;

        void Submit() override;
        void Reset() override;

        void WaitForFence() const override;

    private:
        const gfx::vk::Queue& _queue;
        const ::vk::CommandPool& _parentPool;
        ::vk::Semaphore _signalSemaphore = nullptr;
        ::vk::Fence _fence = nullptr;
    };
}
