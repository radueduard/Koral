//
// Created by radue on 2/21/2026.
//

#pragma once

#include <comandBuffer.h>
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

        gfx::CommandBuffer& BeginRendering(const gfx::Framebuffer* framebuffer) override;
        gfx::CommandBuffer& EndRendering() override;
        gfx::CommandBuffer& BindPipeline(const gfx::ComputePipeline* pipeline) override;
        gfx::CommandBuffer& BindPipeline(const gfx::GraphicsPipeline* pipeline) override;
        gfx::CommandBuffer& BindDescriptorSet(glm::u32 index, const gfx::DescriptorSet* set) override;
        gfx::CommandBuffer& Dispatch(glm::u32 groupCountX, glm::u32 groupCountY, glm::u32 groupCountZ) override;
        gfx::CommandBuffer& Draw(glm::u32 vertexCount, glm::u32 instanceCount, glm::u32 firstVertex, glm::u32 firstInstance) override;
        gfx::CommandBuffer& SetViewport(glm::u32 x, glm::u32 y, glm::u32 width, glm::u32 height) override;
        gfx::CommandBuffer& SetScissor(glm::u32 x, glm::u32 y, glm::u32 width, glm::u32 height) override;
        gfx::CommandBuffer& DrawMesh(const Mesh* mesh, glm::u32 instanceCount, glm::u32 baseInstance) override;
        gfx::CommandBuffer& Blit(const Image* srcImage, const Image* dstImage) override;

        void Submit() override;
        void Reset() override;

        const std::map<std::pair<glm::u32, glm::u32>, glm::u32>& getRemappingTableForBoundPipeline() const;
        void WaitForFence() const override {}

    private:

        bool _recording = false;
        bool _filled = false;
        bool _submitted = false;
        std::vector<std::function<void()>> _commands = {};
    };
}

