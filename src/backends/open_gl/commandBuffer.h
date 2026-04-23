//
// Created by radue on 2/21/2026.
//

#pragma once

#include <commandBuffer.h>
#include <functional>
#include <map>
#include <vector>

namespace gfx::ogl
{
    class GraphicsPipeline;
    class ComputePipeline;

    class CommandBuffer : public gfx::CommandBuffer {
    public:
        void CheckRecording() const;
        explicit CommandBuffer(Flags<Usage> usage);

        gfx::CommandBuffer& Begin() override;
        void End() override;

        gfx::CommandBuffer& BeginRendering(const gfx::Framebuffer* framebuffer, RenderParameters renderParameters) override;
        gfx::CommandBuffer& EndRendering() override;
        gfx::CommandBuffer& BindPipeline(const gfx::ComputePipeline* pipeline) override;
        gfx::CommandBuffer& BindPipeline(const gfx::GraphicsPipeline* pipeline) override;
        gfx::CommandBuffer& BindDescriptorSet(glm::u32 index, const gfx::DescriptorSet* set, bool debug) override;
        gfx::CommandBuffer& BindMesh(const Mesh *mesh) override;
        gfx::CommandBuffer& Barrier(std::vector<gfx::BufferBarrier> bufferBarriers, std::vector<gfx::ImageBarrier> imageBarriers) override;
        gfx::CommandBuffer& Dispatch(glm::u32 groupCountX, glm::u32 groupCountY, glm::u32 groupCountZ) override;
        gfx::CommandBuffer& Draw(glm::u64 vertexCount, glm::u32 instanceCount, glm::u32 firstVertex, glm::u32 firstInstance) override;
        gfx::CommandBuffer& DrawIndexed(glm::u64 indexCount, glm::u32 instanceCount, glm::u32 firstIndex, glm::i32 vertexOffset, glm::u32 firstInstance) override;

        gfx::CommandBuffer& SetViewport(glm::u32 x, glm::u32 y, glm::u32 width, glm::u32 height) override;
        gfx::CommandBuffer& SetScissor(glm::u32 x, glm::u32 y, glm::u32 width, glm::u32 height) override;

        gfx::CommandBuffer& Blit(const Image* srcImage, const Image* dstImage, gfx::Blit blitInfo) override;
        gfx::CommandBuffer& Resolve(const Image *srcImage, const Image *dstImage, gfx::Resolve resolveInfo) override;

        gfx::CommandBuffer& ClearBuffer(const gfx::Buffer *buffer, glm::u64 offset, glm::u64 size) override;
        gfx::CommandBuffer& FillBuffer(const gfx::Buffer *buffer, void *data, glm::u64 offset, glm::u64 size) override;

        gfx::CommandBuffer& Run(const std::function<void(gfx::CommandBuffer&)>& command) override;

        void Submit() override;
        void Reset() override;

        const std::map<std::pair<glm::u32, glm::u32>, glm::u32>& getRemappingTableForBoundPipeline() const;
        void WaitForFence() const override {}

    protected:
        gfx::CommandBuffer & PushConstants(const void *data, glm::u32 size, glm::u32 offset) override;

    private:

        bool _recording = false;
        bool _filled = false;
        bool _submitted = false;
        std::vector<std::function<void()>> _commands = {};
    };
}

