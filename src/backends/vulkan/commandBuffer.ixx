//
// Created by radue on 2/27/2026.
//

module;

#include <functional>
#include "vk_wrapper.h"
#include <vulkan/vulkan.hpp>
#include <glm/glm.hpp>

export module vk.commandBuffer;
import gfx.commandBuffer;
import gfx.structs;
import gfx.resource;
import gfx.framebuffer;
import gfx.image;
import gfx.imageView;
import gfx.graphicsPipeline;
import gfx.computePipeline;
import gfx.descriptorSet;
import gfx.buffer;

namespace gfx::vk
{
    class Queue;

    export class CommandBuffer : public gfx::CommandBuffer, public gfx::vk::Wrapper<::vk::CommandBuffer> {
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
        gfx::CommandBuffer& BeginRendering(ResourceRef<const gfx::Framebuffer> framebuffer, RenderParameters renderParameters) override;
        gfx::CommandBuffer& EndRendering() override;
        gfx::CommandBuffer& SetViewport(glm::u32 x, glm::u32 y, glm::u32 width, glm::u32 height) override;
        gfx::CommandBuffer& SetScissor(glm::u32 x, glm::u32 y, glm::u32 width, glm::u32 height) override;
        gfx::CommandBuffer& BindPipeline(gfx::ResourceRef<const gfx::ComputePipeline> pipeline) override;
        gfx::CommandBuffer& BindPipeline(gfx::ResourceRef<const gfx::GraphicsPipeline> pipeline) override;
        gfx::CommandBuffer& BindDescriptorSet(glm::u32 index, gfx::ResourceRef<const gfx::DescriptorSet> set, bool debug) override;
        gfx::CommandBuffer& BindMesh(gfx::ResourceRef<const gfx::Mesh> mesh) override;
        gfx::CommandBuffer& Barrier(std::vector<gfx::BufferBarrier> bufferBarriers, std::vector<gfx::ImageBarrier> imageBarriers) override;
        gfx::CommandBuffer& Dispatch(glm::u32 groupCountX, glm::u32 groupCountY, glm::u32 groupCountZ) override;
        gfx::CommandBuffer& DispatchIndirect(gfx::ResourceRef<const gfx::Buffer> indirectBuffer, glm::u64 offset) override;
        gfx::CommandBuffer& Draw(glm::u64 vertexCount, glm::u32 instanceCount, glm::u32 firstVertex, glm::u32 firstInstance) override;
        gfx::CommandBuffer& DrawIndexed(glm::u64 indexCount, glm::u32 instanceCount, glm::u32 firstIndex, glm::i32 vertexOffset, glm::u32 firstInstance) override;
        gfx::CommandBuffer& DrawMeshTasks(glm::u32 taskCountX, glm::u32 taskCountY, glm::u32 taskCountZ) override;
        gfx::CommandBuffer& DrawIndirect(gfx::ResourceRef<const gfx::Buffer> indirectBuffer, glm::u64 offset, glm::u32 drawCount, glm::u32 stride) override;
        gfx::CommandBuffer& DrawIndexedIndirect(gfx::ResourceRef<const gfx::Buffer> indirectBuffer, glm::u64 offset, glm::u32 drawCount, glm::u32 stride) override;
        gfx::CommandBuffer& DrawMeshTasksIndirect(gfx::ResourceRef<const gfx::Buffer> indirectBuffer, glm::u64 offset, glm::u32 drawCount, glm::u32 stride) override;

        gfx::CommandBuffer& ClearBuffer(gfx::ResourceRef<const gfx::Buffer> buffer, glm::u64 offset, glm::u64 size) override;
        gfx::CommandBuffer& FillBuffer(gfx::ResourceRef<const gfx::Buffer> buffer, void *data, glm::u64 offset, glm::u64 size) override;
        gfx::CommandBuffer& CopyBuffer(gfx::ResourceRef<const gfx::Buffer> srcBuffer, ResourceRef<const gfx::Buffer> dstBuffer, glm::u64 size, glm::u64 srcOffset, glm::u64 dstOffset) override;

        gfx::CommandBuffer& Blit(gfx::ResourceRef<const gfx::Image> srcImage, gfx::Blit blitInfo) override;
        gfx::CommandBuffer& Blit(gfx::ResourceRef<const gfx::Image> srcImage, gfx::ResourceRef<const gfx::Image> dstImage, gfx::Blit blitInfo) override;
        gfx::CommandBuffer& Resolve(ResourceRef<const gfx::Image> srcImage, gfx::Resolve resolveInfo) override;
        gfx::CommandBuffer& Resolve(gfx::ResourceRef<const gfx::Image> srcImage, gfx::ResourceRef<const gfx::Image> dstImage, gfx::Resolve resolveInfo) override;

        gfx::CommandBuffer& CopyBufferToImage(ResourceRef<const gfx::Buffer> buffer, ResourceRef<const gfx::Image> image, gfx::Copy copyInfo) override;
        gfx::CommandBuffer& CopyImageToBuffer(ResourceRef<const gfx::Image> image, ResourceRef<const gfx::Buffer> buffer, gfx::Copy copyInfo) override;

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
