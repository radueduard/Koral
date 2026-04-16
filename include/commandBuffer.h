//
// Created by radue on 2/21/2026.
//

#pragma once

#include <functional>
#include <memory>
#include <optional>
#include "flags.h"
#include "api.h"

#include <glm/glm.hpp>

#include "sampler.h"

namespace gfx
{
    enum class Filter;
    class Buffer;
    class Image;
    class DescriptorSet;
    class GraphicsPipeline;
    class ComputePipeline;
    class Framebuffer;
    class Mesh;

    enum class PipelineStage {
        // Compute
        Compute = 1 << 0,

        // Vertex pipeline
        VertexInput = 1 << 1,
        VertexShader = 1 << 2,
        FragmentShader = 1 << 3,
        EarlyFragmentTests = 1 << 4,
        LateFragmentTests = 1 << 5,
        ColorAttachmentOutput = 1 << 6,

        // Transfer
        Transfer = 1 << 7,

        // Bottom and top of pipe
        TopOfPipe = 1 << 8,
        BottomOfPipe = 1 << 9
    };

    enum class ResourceAccess {
        // Compute
        ComputeRead,         // COMPUTE_SHADER + SHADER_READ
        ComputeWrite,        // COMPUTE_SHADER + SHADER_WRITE
        ComputeReadWrite,    // COMPUTE_SHADER + SHADER_READ | SHADER_WRITE

        // Vertex pipeline
        VertexBuffer,        // VERTEX_INPUT + VERTEX_ATTRIBUTE_READ
        IndexBuffer,         // VERTEX_INPUT + INDEX_READ
        IndirectBuffer,      // DRAW_INDIRECT + INDIRECT_COMMAND_READ

        // Vertex shader
        VertexShaderRead,    // VERTEX_SHADER + SHADER_READ
        VertexShaderWrite,   // VERTEX_SHADER + SHADER_WRITE
        VertexShaderReadWrite, // VERTEX_SHADER + SHADER_READ | SHADER_WRITE

        // Fragment shader
        FragmentShaderRead,  // FRAGMENT_SHADER + SHADER_READ
        FragmentShaderWrite, // FRAGMENT_SHADER + SHADER_WRITE
        FragmentShaderReadWrite, // FRAGMENT_SHADER + SHADER_READ | SHADER_WRITE

        // Attachments
        ColorAttachment,     // COLOR_ATTACHMENT_OUTPUT + COLOR_ATTACHMENT_WRITE
        DepthAttachment,     // EARLY/LATE_FRAGMENT_TESTS + DEPTH_STENCIL_WRITE
        DepthRead,           // EARLY/LATE_FRAGMENT_TESTS + DEPTH_STENCIL_READ

        // Transfer
        TransferSrc,         // TRANSFER + TRANSFER_READ
        TransferDst,         // TRANSFER + TRANSFER_WRITE

        // General
        AllShaderRead,          // VERTEX_SHADER | FRAGMENT_SHADER | COMPUTE_SHADER + SHADER_READ
        AllShaderWrite,         // VERTEX_SHADER | FRAGMENT_SHADER | COMPUTE_SHADER + SHADER_WRITE
        AllShaderReadWrite,     // VERTEX_SHADER | FRAGMENT_SHADER | COMPUTE_SHADER + SHADER_READ | SHADER_WRITE

        // Present
        Present,             // COLOR_ATTACHMENT_OUTPUT + 0 (no access mask needed)
    };

    class GFX_API BufferBarrier {
    public:
        BufferBarrier(
            const gfx::Buffer& buffer,
            ResourceAccess dstAccess,
            glm::u64 offset = 0,
            glm::u64 size = UINT64_MAX);

        [[nodiscard]] const gfx::Buffer& getBuffer() const { return _buffer; }
        [[nodiscard]] ResourceAccess getDstAccess() const { return _dstAccess; }
        [[nodiscard]] glm::u64 getOffset() const { return _offset; }
        [[nodiscard]] glm::u64 getSize() const { return _size; }

    private:
        const gfx::Buffer& _buffer;
        ResourceAccess _dstAccess;
        glm::u64 _offset;
        glm::u64 _size;
    };

    class GFX_API ImageBarrier {
    public:
        ImageBarrier(
            const gfx::Image& image,
            ResourceAccess dstAccess,
            std::optional<glm::u32> baseMipLevel = std::nullopt,
            std::optional<glm::u32> levelCount = std::nullopt,
            std::optional<glm::u32> baseArrayLayer = std::nullopt,
            std::optional<glm::u32> layerCount = std::nullopt);

        [[nodiscard]] const gfx::Image& getImage() const { return _image; }
        [[nodiscard]] ResourceAccess getDstAccess() const { return _dstAccess; }
        [[nodiscard]] std::optional<glm::u32> getBaseMipLevel() const { return _baseMipLevel; }
        [[nodiscard]] std::optional<glm::u32> getLevelCount() const { return _levelCount; }
        [[nodiscard]] std::optional<glm::u32> getBaseArrayLayer() const { return _baseArrayLayer; }
        [[nodiscard]] std::optional<glm::u32> getLayerCount() const { return _layerCount; }

    private:
        const gfx::Image& _image;
        ResourceAccess _dstAccess;
        std::optional<glm::u32> _baseMipLevel;
        std::optional<glm::u32> _levelCount;
        std::optional<glm::u32> _baseArrayLayer;
        std::optional<glm::u32> _layerCount;
    };

    class GFX_API Blit {
    public:
        glm::ivec3 srcOffset = { 0, 0, 0 };
        glm::ivec3 srcExtent = { -1, -1, -1 };
        glm::ivec3 dstOffset = { 0, 0, 0 };
        glm::ivec3 dstExtent = { -1, -1, -1 };
        glm::u32 srcBaseArrayLayer = 0;
        glm::u32 dstBaseArrayLayer = 0;
        glm::u32 layerCount = 1;
        glm::u32 srcMipLevel = 0;
        glm::u32 dstMipLevel = 0;
        gfx::Filter filtering = gfx::Filter::eNearest;
    };

    class GFX_API Resolve {
    public:
        glm::ivec3 srcOffset = { 0, 0, 0 };
        glm::ivec3 srcExtent = { -1, -1, -1 };
        glm::ivec3 dstOffset = { 0, 0, 0 };
        glm::ivec3 dstExtent = { -1, -1, -1 };
        glm::u32 srcBaseArrayLayer = 0;
        glm::u32 dstBaseArrayLayer = 0;
        glm::u32 layerCount = 1;
        glm::u32 srcMipLevel = 0;
        glm::u32 dstMipLevel = 0;
    };

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
        virtual CommandBuffer& BindMesh(const Mesh* mesh);

        template<typename T> requires std::is_trivially_copyable_v<T>
        CommandBuffer& PushConstants(const T& data, const glm::u32 offset = 0) {
            return PushConstants(&data, sizeof(T), offset);
        }

        virtual CommandBuffer& Barrier(std::vector<BufferBarrier> bufferBarriers = {}, std::vector<ImageBarrier> imageBarriers = {}) = 0;

        virtual CommandBuffer& Dispatch(glm::u32 groupCountX, glm::u32 groupCountY, glm::u32 groupCountZ) = 0;

        virtual CommandBuffer& Draw(glm::u64 vertexCount = UINT64_MAX, glm::u32 instanceCount = 1, glm::u32 firstVertex = 0, glm::u32 firstInstance = 0);
        virtual CommandBuffer& DrawIndexed(glm::u64 indexCount = UINT64_MAX, glm::u32 instanceCount = 1, glm::u32 firstIndex = 0, glm::i32 vertexOffset = 0, glm::u32 firstInstance = 0);

        virtual CommandBuffer& Blit(
            const Image* srcImage,
            const Image* dstImage = nullptr,
            Blit blitInfo = {}) = 0;

        virtual CommandBuffer& Resolve(
            const Image* srcImage,
            const Image* dstImage = nullptr,
            Resolve resolveInfo = {}) = 0;

        virtual CommandBuffer& Run(const std::function<void(CommandBuffer&)>& command) = 0;

        virtual void Submit() = 0;
        virtual void Reset() = 0;

        CommandBuffer& BufferBarrier(const BufferBarrier& barrier) { return Barrier({ barrier }, {}); }
        CommandBuffer& ImageBarrier(const ImageBarrier& barrier) { return Barrier({}, { barrier }); }

        CommandBuffer& DrawMesh(const Mesh* mesh, glm::u32 instanceCount , glm::u32 baseInstance);
        CommandBuffer& DrawSubMesh(const Mesh *mesh, glm::u32 baseIndex, glm::u32 indexCount);

        static std::unique_ptr<CommandBuffer> Create(Flags<Usage> usage);

        virtual void WaitForFence() const = 0;

    protected:
        struct {
            std::optional<const Framebuffer*> boundFramebuffer = std::nullopt;
            std::optional<const ComputePipeline*> boundComputePipeline = std::nullopt;
            std::optional<const GraphicsPipeline*> boundGraphicsPipeline = std::nullopt;
            std::optional<const Mesh*> boundMesh = std::nullopt;

            bool viewportSet = false;
            bool scissorSet = false;
        } _state;

        explicit CommandBuffer(const Flags<Usage> usage) : _usage(usage) {}
        Flags<Usage> _usage;

        virtual CommandBuffer& PushConstants(const void* data, glm::u32 size, glm::u32 offset) = 0;
    };
}

