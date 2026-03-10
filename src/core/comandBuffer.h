//
// Created by radue on 2/21/2026.
//

#pragma once
#include <glm/fwd.hpp>

#include "computePipeline.h"
#include "descriptorBinding.h"
#include "graphicsPipeline.h"
#include "utils/flags.h"

namespace gfx
{
    class Mesh;

    class CommandBuffer
    {
    public:
        virtual ~CommandBuffer() = default;
        CommandBuffer& operator=(const CommandBuffer&) = delete;
        CommandBuffer(const CommandBuffer&) = delete;

        enum class Usage
        {
            eGraphics = 1 << 0,
            eCompute = 1 << 1,
            eTransfer = 1 << 2
        };

        virtual CommandBuffer& Begin() = 0;
        virtual void End() = 0;

        virtual CommandBuffer& BeginRendering(const Framebuffer* framebuffer) = 0;
        virtual CommandBuffer& EndRendering() = 0;
        virtual CommandBuffer& SetViewport(glm::u32 x, glm::u32 y, glm::u32 width, glm::u32 height) = 0;
        virtual CommandBuffer& SetScissor(glm::u32 x, glm::u32 y, glm::u32 width, glm::u32 height) = 0;
        virtual CommandBuffer& BindPipeline(const ComputePipeline* pipeline);
        virtual CommandBuffer& BindPipeline(const GraphicsPipeline* pipeline);
        virtual CommandBuffer& BindDescriptorSet(glm::u32 index, const DescriptorSet* descriptorSet) = 0;
        virtual CommandBuffer& Dispatch(glm::u32 groupCountX, glm::u32 groupCountY, glm::u32 groupCountZ) = 0;
        virtual CommandBuffer& Draw(glm::u32 vertexCount, glm::u32 instanceCount, glm::u32 firstVertex, glm::u32 firstInstance) = 0;
        virtual CommandBuffer& DrawMesh(const Mesh* mesh, glm::u32 instanceCount, glm::u32 baseInstance) = 0;

        virtual void Submit() = 0;
        virtual void Reset() = 0;

        static std::unique_ptr<CommandBuffer> Create(Flags<Usage> usage);

        virtual void WaitForFence() const = 0;

    protected:
        struct {
            std::optional<const Framebuffer*> boundFramebuffer = std::nullopt;
            std::optional<const ComputePipeline*> boundComputePipeline = std::nullopt;
            std::optional<const GraphicsPipeline*> boundGraphicsPipeline = std::nullopt;
        } _state;

        explicit CommandBuffer(const Flags<Usage> usage) : _usage(usage) {}
        Flags<Usage> _usage;
    };
}

