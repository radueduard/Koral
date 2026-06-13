//
// Created by radue on 2/21/2026.
//

module;

#include <glm/glm.hpp>

export module gfx:ogl_commandBuffer;
import :ogl_types;

import std;
import flags;
import resource;
import :commandBuffer;

namespace gfx::ogl
{
    class CommandBuffer : public gfx::CommandBuffer {
    public:
        void CheckRecording() const;
        explicit CommandBuffer(Flags<Usage> usage);

        gfx::CommandBuffer& Begin() override;
        void End() override;

        gfx::CommandBuffer& BeginRendering(RenderParameters renderParameters) override;
        gfx::CommandBuffer& BeginRendering(ResourceRef<const gfx::Framebuffer> framebuffer, RenderParameters renderParameters) override;
        gfx::CommandBuffer& EndRendering() override;
        gfx::CommandBuffer& BindPipeline(ResourceRef<const gfx::ComputePipeline> pipeline) override;
        gfx::CommandBuffer& BindPipeline(ResourceRef<const gfx::GraphicsPipeline> pipeline) override;
        gfx::CommandBuffer& BindDescriptorSet(glm::u32 index, ResourceRef<const gfx::DescriptorSet> set, bool debug) override;
        gfx::CommandBuffer& BindMesh(ResourceRef<const Mesh> mesh) override;
        gfx::CommandBuffer& Barrier(std::vector<gfx::BufferBarrier> bufferBarriers, std::vector<gfx::ImageBarrier> imageBarriers) override;
        gfx::CommandBuffer& Dispatch(glm::u32 groupCountX, glm::u32 groupCountY, glm::u32 groupCountZ) override;
        gfx::CommandBuffer& DispatchIndirect(ResourceRef<const gfx::Buffer> indirectBuffer, glm::u64 offset) override;
        gfx::CommandBuffer& Draw(glm::u64 vertexCount, glm::u32 instanceCount, glm::u32 firstVertex, glm::u32 firstInstance) override;
        gfx::CommandBuffer& DrawIndexed(glm::u64 indexCount, glm::u32 instanceCount, glm::u32 firstIndex, glm::i32 vertexOffset, glm::u32 firstInstance) override;
        gfx::CommandBuffer& DrawMeshTasks(glm::u32 taskCountX, glm::u32 taskCountY, glm::u32 taskCountZ) override;
        gfx::CommandBuffer& DrawIndirect(ResourceRef<const gfx::Buffer> indirectBuffer, glm::u64 offset, glm::u32 drawCount, glm::u32 stride) override;
        gfx::CommandBuffer& DrawIndexedIndirect(ResourceRef<const gfx::Buffer> indirectBuffer, glm::u64 offset, glm::u32 drawCount, glm::u32 stride) override;
        gfx::CommandBuffer& DrawMeshTasksIndirect(ResourceRef<const gfx::Buffer> indirectBuffer, glm::u64 offset, glm::u32 drawCount, glm::u32 stride) override;

        gfx::CommandBuffer& SetViewport(glm::u32 x, glm::u32 y, glm::u32 width, glm::u32 height) override;
        gfx::CommandBuffer& SetScissor(glm::u32 x, glm::u32 y, glm::u32 width, glm::u32 height) override;

        gfx::CommandBuffer& Blit(ResourceRef<const gfx::Image> srcImage, gfx::Blit blitInfo) override;
        gfx::CommandBuffer& Blit(ResourceRef<const gfx::Image> srcImage, ResourceRef<const gfx::Image> dstImage, gfx::Blit blitInfo) override;
        gfx::CommandBuffer& Resolve(ResourceRef<const gfx::Image> srcImage, gfx::Resolve resolveInfo) override;
        gfx::CommandBuffer& Resolve(ResourceRef<const gfx::Image> srcImage, ResourceRef<const gfx::Image> dstImage, gfx::Resolve resolveInfo) override;

        gfx::CommandBuffer& ClearBuffer(ResourceRef<const gfx::Buffer> buffer, glm::u64 offset, glm::u64 size) override;
        gfx::CommandBuffer& FillBuffer(ResourceRef<const gfx::Buffer> buffer, void *data, glm::u64 offset, glm::u64 size) override;
        gfx::CommandBuffer& CopyBuffer(ResourceRef<const gfx::Buffer> srcBuffer, ResourceRef<const gfx::Buffer> dstBuffer, glm::u64 size, glm::u64 srcOffset, glm::u64 dstOffset) override;

        gfx::CommandBuffer& Run(const std::function<void(gfx::CommandBuffer&)>& command) override;

        gfx::CommandBuffer& CopyBufferToImage(ResourceRef<const gfx::Buffer> buffer, ResourceRef<const gfx::Image> image, gfx::Copy copyInfo) override;
        gfx::CommandBuffer& CopyImageToBuffer(ResourceRef<const gfx::Image> image, ResourceRef<const gfx::Buffer> buffer, gfx::Copy copyInfo) override;

        void Submit() override;
        void Reset() override;

        const std::map<std::pair<glm::u32, glm::u32>, glm::u32>& getRemappingTableForBoundPipeline() const;
        void WaitForFence() const override {}

    protected:
        gfx::CommandBuffer & PushConstants(const void *data, glm::u32 size, glm::u32 offset) override;

    public:
        ~CommandBuffer() noexcept override = default;
    private:
        bool _recording = false;
        bool _filled = false;
        bool _submitted = false;
        std::vector<std::function<void()>> _commands = {};
    };
}

