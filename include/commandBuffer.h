//
// Created by radue on 2/21/2026.
//

#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <glm/fwd.hpp>
#include "flags.h"
#include "api.h"

namespace gfx
{
    class Image;
    class DescriptorSet;
    class GraphicsPipeline;
    class ComputePipeline;
    class Framebuffer;
    class Mesh;

    class GFX_API CommandBuffer
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

        virtual CommandBuffer& BeginRendering(const Framebuffer* framebuffer = nullptr);
        virtual CommandBuffer& EndRendering();
        virtual CommandBuffer& SetViewport(glm::u32 x, glm::u32 y, glm::u32 width, glm::u32 height);
        virtual CommandBuffer& SetScissor(glm::u32 x, glm::u32 y, glm::u32 width, glm::u32 height);
        virtual CommandBuffer& BindPipeline(const ComputePipeline* pipeline);
        virtual CommandBuffer& BindPipeline(const GraphicsPipeline* pipeline);
        virtual CommandBuffer& BindDescriptorSet(glm::u32 index, const DescriptorSet* descriptorSet) = 0;

        template<typename T> requires std::is_trivially_copyable_v<T>
        CommandBuffer& PushConstants(const T& data, const glm::u32 offset = 0) {
            return PushConstants(&data, sizeof(T), offset);
        }

        virtual CommandBuffer& Dispatch(glm::u32 groupCountX, glm::u32 groupCountY, glm::u32 groupCountZ) = 0;

        virtual CommandBuffer& Draw(glm::u32 vertexCount, glm::u32 instanceCount, glm::u32 firstVertex, glm::u32 firstInstance);
        virtual CommandBuffer& DrawMesh(const Mesh* mesh, glm::u32 instanceCount , glm::u32 baseInstance);
        virtual CommandBuffer& DrawSubMesh(const Mesh *mesh, glm::u32 baseIndex, glm::u32 indexCount);

        virtual CommandBuffer& Blit(const Image* srcImage, const Image* dstImage = nullptr) = 0;
        virtual CommandBuffer& Run(const std::function<void(CommandBuffer&)>& command) = 0;

        virtual void Submit() = 0;
        virtual void Reset() = 0;

        static std::unique_ptr<CommandBuffer> Create(Flags<Usage> usage);

        virtual void WaitForFence() const = 0;

    protected:
        struct {
            std::optional<const Framebuffer*> boundFramebuffer = std::nullopt;
            std::optional<const ComputePipeline*> boundComputePipeline = std::nullopt;
            std::optional<const GraphicsPipeline*> boundGraphicsPipeline = std::nullopt;

            bool viewportSet = false;
            bool scissorSet = false;
        } _state;

        explicit CommandBuffer(const Flags<Usage> usage) : _usage(usage) {}
        Flags<Usage> _usage;

        virtual CommandBuffer& PushConstants(const void* data, glm::u32 size, glm::u32 offset) = 0;
    };
}

