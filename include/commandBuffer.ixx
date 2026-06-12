//
// Created by radue on 2/21/2026.
//

module;

#include "api.h"
#include <glm/glm.hpp>

export module gfx.commandBuffer;

import std;
import gfx.structs;
import gfx.resource;
import gfx.flags;
import gfx.framebuffer;
import gfx.computePipeline;
import gfx.graphicsPipeline;
import gfx.descriptorSet;
import gfx.image;

namespace gfx
{
    class Buffer;

    export class GFX_API CommandBuffer
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

        virtual CommandBuffer& BeginRendering(RenderParameters renderParameters = {});
        virtual CommandBuffer& BeginRendering(ResourceRef<const Framebuffer> framebuffer, RenderParameters renderParameters = {});
        virtual CommandBuffer& EndRendering();
        virtual CommandBuffer& SetViewport(glm::u32 x, glm::u32 y, glm::u32 width, glm::u32 height);
        virtual CommandBuffer& SetScissor(glm::u32 x, glm::u32 y, glm::u32 width, glm::u32 height);
        virtual CommandBuffer& BindPipeline(ResourceRef<const gfx::ComputePipeline> pipeline);
        virtual CommandBuffer& BindPipeline(ResourceRef<const gfx::GraphicsPipeline> pipeline);
        virtual CommandBuffer& BindDescriptorSet(glm::u32 index, ResourceRef<const gfx::DescriptorSet> descriptorSet, bool debug = false) = 0;
        virtual CommandBuffer& BindMesh(ResourceRef<const Mesh> mesh);


        template<typename T> requires std::is_trivially_copyable_v<T>
        CommandBuffer& PushConstants(const T& data, const glm::u32 offset = 0) {
            return PushConstants(&data, sizeof(T), offset);
        }

        virtual CommandBuffer& Barrier(std::vector<BufferBarrier> bufferBarriers = {}, std::vector<ImageBarrier> imageBarriers = {}) = 0;

        virtual CommandBuffer& Dispatch(glm::u32 groupCountX = 1, glm::u32 groupCountY = 1, glm::u32 groupCountZ = 1);
        virtual CommandBuffer& DispatchIndirect(ResourceRef<const gfx::Buffer> indirectBuffer, glm::u64 offset = 0);

        virtual CommandBuffer& Draw(glm::u64 vertexCount = UINT64_MAX, glm::u32 instanceCount = 1, glm::u32 firstVertex = 0, glm::u32 firstInstance = 0);
        virtual CommandBuffer& DrawIndexed(glm::u64 indexCount = UINT64_MAX, glm::u32 instanceCount = 1, glm::u32 firstIndex = 0, glm::i32 vertexOffset = 0, glm::u32 firstInstance = 0);
        virtual CommandBuffer& DrawMeshTasks(glm::u32 taskCountX = 1, glm::u32 taskCountY = 1, glm::u32 taskCountZ = 1);

        virtual CommandBuffer& DrawIndirect(ResourceRef<const Buffer> indirectBuffer, glm::u64 offset = 0, glm::u32 drawCount = 1, glm::u32 stride = 0);
        virtual CommandBuffer& DrawIndexedIndirect(ResourceRef<const Buffer> indirectBuffer, glm::u64 offset = 0, glm::u32 drawCount = 1, glm::u32 stride = 0);
        virtual CommandBuffer& DrawMeshTasksIndirect(ResourceRef<const Buffer> indirectBuffer, glm::u64 offset = 0, glm::u32 drawCount = 1, glm::u32 stride = 0);

        virtual CommandBuffer& ClearBuffer(ResourceRef<const Buffer> buffer, glm::u64 offset = 0, glm::u64 size = UINT64_MAX) = 0;
        virtual CommandBuffer& FillBuffer(ResourceRef<const Buffer> buffer, void* data, glm::u64 offset = 0, glm::u64 size = UINT64_MAX) = 0;
        virtual CommandBuffer& CopyBuffer(ResourceRef<const Buffer> srcBuffer, ResourceRef<const Buffer> dstBuffer, glm::u64 size = UINT64_MAX, glm::u64 srcOffset = 0, glm::u64 dstOffset = 0) = 0;

        virtual CommandBuffer& Blit(
            ResourceRef<const gfx::Image> srcImage,
            gfx::Blit blitInfo = {}) = 0;

        virtual CommandBuffer& Blit(
            ResourceRef<const gfx::Image> srcImage,
            ResourceRef<const gfx::Image> dstImage,
            gfx::Blit blitInfo = {}) = 0;

        virtual CommandBuffer& Resolve(
            ResourceRef<const gfx::Image> srcImage,
            gfx::Resolve resolveInfo = {}) = 0;

        virtual CommandBuffer& Resolve(
            ResourceRef<const gfx::Image> srcImage,
            ResourceRef<const gfx::Image> dstImage,
            gfx::Resolve resolveInfo = {}) = 0;

        CommandBuffer& GenerateMipmaps(ResourceRef<const gfx::Image> image);

        virtual CommandBuffer& CopyBufferToImage(ResourceRef<const gfx::Buffer> buffer, ResourceRef<const gfx::Image> image, gfx::Copy copyInfo = {}) = 0;
        virtual CommandBuffer& CopyImageToBuffer(ResourceRef<const gfx::Image> image, ResourceRef<const gfx::Buffer> buffer, gfx::Copy copyInfo = {}) = 0;

        virtual CommandBuffer& Run(const std::function<void(gfx::CommandBuffer&)>& command) = 0;

        template<typename Func> requires std::is_invocable_v<Func, gfx::CommandBuffer&>
        CommandBuffer& If(
            const std::function<bool()>& condition,
            Func&& trueCommands,
            std::optional<Func> falseCommands = std::nullopt
        ) {
            if (condition()) {
                trueCommands(*this);
            } else if (falseCommands) {
                falseCommands->operator()(*this);
            }
            return *this;
        }

        template<typename... Args> requires (std::is_convertible_v<std::invoke_result_t<Args, gfx::CommandBuffer&>, void> && ...)
        CommandBuffer& Condition(
            const std::function<glm::u8()>& condition,
            Args... commands
        ) {
            const glm::u8 cond = condition();
            const std::array<std::function<void(gfx::CommandBuffer&)>, sizeof...(Args)> commandArray = { commands... };
            commandArray[cond](*this);
            return *this;
        }

        template<std::ranges::range R, typename Func> requires std::is_invocable_v<Func, gfx::CommandBuffer&, std::ranges::range_value_t<R>>
        CommandBuffer& ForEach(R&& range, Func&& func) {
            for (auto&& item : range) {
                func(*this, item);
            }
            return *this;
        }

        static void SingleTimeCommand(const std::function<void(gfx::CommandBuffer&)>& command, Usage usage = Usage::eGraphics);

        virtual void Submit() = 0;
        virtual void Reset() = 0;

        CommandBuffer& BufferBarrier(const BufferBarrier& barrier) { return Barrier({ barrier }, {}); }
        CommandBuffer& ImageBarrier(const ImageBarrier& barrier) { return Barrier({}, { barrier }); }

        CommandBuffer& DrawMesh(ResourceRef<const Mesh> mesh, glm::u32 instanceCount , glm::u32 baseInstance);
        CommandBuffer& DrawSubMesh(ResourceRef<const Mesh> mesh, glm::u32 baseIndex, glm::u32 indexCount);

        static std::unique_ptr<CommandBuffer> Create(Flags<Usage> usage);

        virtual void WaitForFence() const = 0;

    protected:
        struct {
            std::optional<gfx::ResourceRef<const Framebuffer>> boundFramebuffer = std::nullopt;
            std::optional<gfx::ResourceRef<const ComputePipeline>> boundComputePipeline = std::nullopt;
            std::optional<gfx::ResourceRef<const GraphicsPipeline>> boundGraphicsPipeline = std::nullopt;
            std::optional<gfx::ResourceRef<const Mesh>> boundMesh = std::nullopt;

            bool viewportSet = false;
            bool scissorSet = false;
        } _state;

        explicit CommandBuffer(const Flags<Usage> usage)
            : _usage(usage) {}
        Flags<Usage> _usage;

        virtual CommandBuffer& PushConstants(const void* data, glm::u32 size, glm::u32 offset) = 0;
    };
}

