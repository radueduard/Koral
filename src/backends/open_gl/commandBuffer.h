//
// Created by radue on 2/21/2026.
//

#pragma once

#include <GL/glew.h>

#include <commandBuffer.h>
#include <functional>
#include <map>
#include <vector>

#include "shader.h" // BindlessSamplerArray

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

        gfx::CommandBuffer& BeginRendering(RenderParameters renderParameters) override;
        gfx::CommandBuffer& doBeginRendering(ResourceRef<const gfx::Framebuffer> framebuffer, RenderParameters renderParameters) override;
        gfx::CommandBuffer& EndRendering() override;
        gfx::CommandBuffer& doBindComputePipeline(gfx::ResourceRef<const gfx::ComputePipeline> pipeline) override;
        gfx::CommandBuffer& doBindGraphicsPipeline(gfx::ResourceRef<const gfx::GraphicsPipeline> pipeline) override;
        gfx::CommandBuffer& doBindDescriptorSet(glm::u32 index, gfx::ResourceRef<const gfx::DescriptorSet> set, bool debug) override;
        gfx::CommandBuffer& doBindMesh(gfx::ResourceRef<const Mesh> mesh) override;
        gfx::CommandBuffer& doBarrier(std::vector<gfx::BufferBarrier> bufferBarriers, std::vector<gfx::ImageBarrier> imageBarriers) override;
        gfx::CommandBuffer& BeginDebugLabel(const std::string& label, glm::vec4 color) override;
        gfx::CommandBuffer& EndDebugLabel() override;
        gfx::CommandBuffer& InsertDebugLabel(const std::string& label, glm::vec4 color) override;
        gfx::CommandBuffer& Dispatch(glm::u32 groupCountX, glm::u32 groupCountY, glm::u32 groupCountZ) override;
        gfx::CommandBuffer& doDispatchIndirect(gfx::ResourceRef<const gfx::Buffer> indirectBuffer, glm::u64 offset) override;
        gfx::CommandBuffer& Draw(glm::u64 vertexCount, glm::u32 instanceCount, glm::u32 firstVertex, glm::u32 firstInstance) override;
        gfx::CommandBuffer& DrawIndexed(glm::u64 indexCount, glm::u32 instanceCount, glm::u32 firstIndex, glm::i32 vertexOffset, glm::u32 firstInstance) override;
        gfx::CommandBuffer& doDrawIndirect(gfx::ResourceRef<const gfx::Buffer> indirectBuffer, glm::u64 offset, glm::u32 drawCount, glm::u32 stride) override;
        gfx::CommandBuffer& doDrawIndexedIndirect(gfx::ResourceRef<const gfx::Buffer> indirectBuffer, glm::u64 offset, glm::u32 drawCount, glm::u32 stride) override;

        gfx::CommandBuffer& SetViewport(glm::u32 x, glm::u32 y, glm::u32 width, glm::u32 height) override;
        gfx::CommandBuffer& SetScissor(glm::u32 x, glm::u32 y, glm::u32 width, glm::u32 height) override;

        // Dynamic-state overrides. GL applies the pipeline defaults during Bind and
        // replays commands in order, so an override just records the matching GL call
        // after the bind.
        gfx::CommandBuffer& SetLineWidth(float lineWidth) override;
        gfx::CommandBuffer& SetDepthBias(float constantFactor, float clamp, float slopeFactor) override;
        gfx::CommandBuffer& SetBlendConstants(glm::vec4 constants) override;
        gfx::CommandBuffer& SetStencilCompareMask(StencilFace face, glm::u32 compareMask) override;
        gfx::CommandBuffer& SetStencilWriteMask(StencilFace face, glm::u32 writeMask) override;
        gfx::CommandBuffer& SetStencilReference(StencilFace face, glm::u32 reference) override;
        gfx::CommandBuffer& SetCullMode(Flags<CullMode> cullMode) override;
        gfx::CommandBuffer& SetFrontFace(FrontFace frontFace) override;
        gfx::CommandBuffer& SetDepthTestEnable(bool enable) override;
        gfx::CommandBuffer& SetDepthWriteEnable(bool enable) override;
        gfx::CommandBuffer& SetDepthCompareOp(CompareOp compareOp) override;
        gfx::CommandBuffer& SetStencilTestEnable(bool enable) override;
        gfx::CommandBuffer& SetStencilOp(StencilFace face, StencilOp failOp, StencilOp passOp, StencilOp depthFailOp, CompareOp compareOp) override;
        gfx::CommandBuffer& SetDepthBiasEnable(bool enable) override;
        gfx::CommandBuffer& SetRasterizerDiscardEnable(bool enable) override;
        gfx::CommandBuffer& SetPrimitiveRestartEnable(bool enable) override;

        gfx::CommandBuffer& doBlit(ResourceRef<const Image> srcImage, gfx::Blit blitInfo) override;
        gfx::CommandBuffer& doBlit(gfx::ResourceRef<const Image> srcImage, gfx::ResourceRef<const Image> dstImage, gfx::Blit blitInfo) override;
        gfx::CommandBuffer& doGenerateMipmaps(gfx::ResourceRef<const gfx::Image> image) override;
        gfx::CommandBuffer& doResolve(ResourceRef<const Image> srcImage, gfx::Resolve resolveInfo) override;
        gfx::CommandBuffer& doResolve(gfx::ResourceRef<const Image> srcImage, gfx::ResourceRef<const Image> dstImage, gfx::Resolve resolveInfo) override;

        gfx::CommandBuffer& doClearBuffer(gfx::ResourceRef<const gfx::Buffer> buffer, glm::u64 offset, glm::u64 size) override;
        gfx::CommandBuffer& doClearColorImage(gfx::ResourceRef<const gfx::Image> image, glm::vec4 color) override;
        gfx::CommandBuffer& doFillBuffer(gfx::ResourceRef<const gfx::Buffer> buffer, void *data, glm::u64 offset, glm::u64 size) override;
        gfx::CommandBuffer& doCopyBuffer(ResourceRef<const gfx::Buffer> srcBuffer, ResourceRef<const gfx::Buffer> dstBuffer, glm::u64 size, glm::u64 srcOffset, glm::u64 dstOffset) override;

        gfx::CommandBuffer& Run(const std::function<void(gfx::CommandBuffer&)>& command) override;

        gfx::CommandBuffer& doCopyBufferToImage(ResourceRef<const gfx::Buffer> buffer, ResourceRef<const gfx::Image> image, gfx::Copy copyInfo) override;
        gfx::CommandBuffer& doCopyImageToBuffer(ResourceRef<const gfx::Image> image, ResourceRef<const gfx::Buffer> buffer, gfx::Copy copyInfo) override;

        gfx::VoidResult Submit() override;
        void Reset() override;

        const std::map<std::pair<glm::u32, glm::u32>, glm::u32>& getRemappingTableForBoundPipeline() const;

        // Bound-pipeline accessors used by DescriptorSet::bind to drive bindless
        // material arrays (see BindlessSamplerArray): the linked program object and
        // the bindless arrays the pipeline declared.
        [[nodiscard]] GLuint getBoundPipelineProgram() const;
        [[nodiscard]] const std::vector<BindlessSamplerArray>& getBoundPipelineBindlessArrays() const;

        void WaitForFence() const override {}

    protected:
        gfx::CommandBuffer & PushConstants(const void *data, glm::u32 size, glm::u32 offset) override;

    private:
        // Issue the default full-framebuffer viewport/scissor at replay time when the
        // caller did not set them explicitly.
        void applyDefaultViewportScissor();

        // Bound framebuffer extent (window extent for the default framebuffer). Used to
        // convert the API's top-left viewport/scissor origin to GL's bottom-left.
        [[nodiscard]] glm::uvec2 currentFramebufferExtent() const;

        // Re-issue glStencilFuncSeparate from the shadow copy below. GL fuses
        // func/reference/compare-mask into one call while the API exposes them as
        // independent dynamic states, so each setter updates its field and reapplies.
        void applyStencilFunc(StencilFace face);
        struct StencilFuncShadow {
            GLenum compareOp = GL_ALWAYS;
            glm::u32 reference = 0;
            glm::u32 compareMask = 0xFFFFFFFFu;
        };
        StencilFuncShadow _stencilShadow[2] = {}; // [0]=front, [1]=back

        // Record a GL operation. Normally the operation is appended to _commands and
        // replayed at Submit. But a command may be recorded from *inside* another
        // command's replay (Run's lambda calls Dispatch/Barrier/etc.); when that
        // happens (_executing) there is no more recording to do — the surrounding
        // replay is already running — so the operation executes in place, preserving
        // order and avoiding mutation of _commands mid-iteration.
        void enqueue(std::function<void()> op) {
            if (_executing) op();
            else _commands.emplace_back(std::move(op));
        }

        bool _recording = false;
        bool _filled = false;
        bool _submitted = false;
        bool _executing = false; // true while Submit is replaying _commands
        std::vector<std::function<void()>> _commands = {};
    };
}

