//
// Created by radue on 2/27/2026.
//

#pragma once

#include <functional>
#include "vk_wrapper.h"
#include <vulkan/vulkan.hpp>

#include <commandBuffer.h>

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
        gfx::CommandBuffer& BeginRendering(RenderParameters renderParameters) override;
        gfx::CommandBuffer& BeginRendering(gfx::ResourceRef<gfx::Framebuffer> framebuffer, RenderParameters renderParameters) override;
        gfx::CommandBuffer& EndRendering() override;
        gfx::CommandBuffer& SetViewport(glm::u32 x, glm::u32 y, glm::u32 width, glm::u32 height) override;
        gfx::CommandBuffer& SetScissor(glm::u32 x, glm::u32 y, glm::u32 width, glm::u32 height) override;
        gfx::CommandBuffer& BindPipeline(gfx::ResourceRef<gfx::ComputePipeline> pipeline) override;
        gfx::CommandBuffer& BindPipeline(gfx::ResourceRef<gfx::GraphicsPipeline> pipeline) override;
        gfx::CommandBuffer& BindDescriptorSet(glm::u32 index, gfx::ResourceRef<gfx::DescriptorSet> set, bool debug) override;
        gfx::CommandBuffer& BindMesh(gfx::ResourceRef<Mesh> mesh) override;
        gfx::CommandBuffer& Barrier(std::vector<gfx::BufferBarrier> bufferBarriers, std::vector<gfx::ImageBarrier> imageBarriers) override;
        gfx::CommandBuffer& Dispatch(glm::u32 groupCountX, glm::u32 groupCountY, glm::u32 groupCountZ) override;
        gfx::CommandBuffer& Draw(glm::u64 vertexCount, glm::u32 instanceCount, glm::u32 firstVertex, glm::u32 firstInstance) override;
        gfx::CommandBuffer& DrawIndexed(glm::u64 indexCount, glm::u32 instanceCount, glm::u32 firstIndex, glm::i32 vertexOffset, glm::u32 firstInstance) override;

        gfx::CommandBuffer& ClearBuffer(gfx::ResourceRef<gfx::Buffer> buffer, glm::u64 offset, glm::u64 size) override;
        gfx::CommandBuffer& FillBuffer(gfx::ResourceRef<gfx::Buffer> buffer, void *data, glm::u64 offset, glm::u64 size) override;
        gfx::CommandBuffer& CopyBuffer(ResourceRef<gfx::Buffer> srcBuffer, ResourceRef<gfx::Buffer> dstBuffer, glm::u64 size, glm::u64 srcOffset, glm::u64 dstOffset) override;

        gfx::CommandBuffer& Blit(ResourceRef<Image> srcImage, gfx::Blit blitInfo) override;
        gfx::CommandBuffer& Blit(gfx::ResourceRef<gfx::Image> srcImage, gfx::ResourceRef<gfx::Image> dstImage, gfx::Blit blitInfo) override;
        gfx::CommandBuffer& Resolve(ResourceRef<Image> srcImage, gfx::Resolve resolveInfo) override;
        gfx::CommandBuffer& Resolve(gfx::ResourceRef<Image> srcImage, gfx::ResourceRef<Image> dstImage, gfx::Resolve resolveInfo) override;

        gfx::CommandBuffer& CopyBufferToImage(ResourceRef<gfx::Buffer> buffer, ResourceRef<gfx::Image> image, gfx::Copy copyInfo) override;
        gfx::CommandBuffer& CopyImageToBuffer(ResourceRef<gfx::Image> image, ResourceRef<gfx::Buffer> buffer, gfx::Copy copyInfo) override;

        gfx::CommandBuffer& Run(const std::function<void(gfx::CommandBuffer&)>& command) override;

        void Submit() override;
        void Reset() override;

        void WaitForFence() const override;

    protected:
        gfx::CommandBuffer & PushConstants(const void *data, glm::u32 size, glm::u32 offset) override;
        gfx::Resource<gfx::Image> _resolveHelperImage;

    private:
        const gfx::vk::Queue& _queue;
        const ::vk::CommandPool& _parentPool;
        ::vk::Semaphore _signalSemaphore = nullptr;
        ::vk::Fence _fence = nullptr;
    };
}
