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

namespace kor::ogl
{
    class GraphicsPipeline;
    class ComputePipeline;

    class CommandBuffer : public kor::CommandBuffer {
    public:
        void CheckRecording() const;
        explicit CommandBuffer(Flags<Usage> usage);

        kor::CommandBuffer& Begin() override;
        void End() override;

        kor::CommandBuffer& BeginRendering(RenderParameters renderParameters) override;
        kor::CommandBuffer& doBeginRendering(ResourceRef<const kor::Framebuffer> framebuffer, RenderParameters renderParameters) override;
        kor::CommandBuffer& EndRendering() override;
        kor::CommandBuffer& doBindComputePipeline(kor::ResourceRef<const kor::ComputePipeline> pipeline) override;
        kor::CommandBuffer& doBindGraphicsPipeline(kor::ResourceRef<const kor::GraphicsPipeline> pipeline) override;
        kor::CommandBuffer& doBindDescriptorSet(glm::u32 index, kor::ResourceRef<const kor::DescriptorSet> set, bool debug) override;
        kor::CommandBuffer& doBindMesh(kor::ResourceRef<const Mesh> mesh) override;
        kor::CommandBuffer& doBarrier(std::vector<kor::BufferBarrier> bufferBarriers, std::vector<kor::ImageBarrier> imageBarriers) override;
        kor::CommandBuffer& BeginDebugLabel(const std::string& label, glm::vec4 color) override;
        kor::CommandBuffer& EndDebugLabel() override;
        kor::CommandBuffer& InsertDebugLabel(const std::string& label, glm::vec4 color) override;
        kor::CommandBuffer& Dispatch(glm::u32 groupCountX, glm::u32 groupCountY, glm::u32 groupCountZ) override;
        kor::CommandBuffer& doDispatchIndirect(kor::ResourceRef<const kor::Buffer> indirectBuffer, glm::u64 offset) override;
        kor::CommandBuffer& Draw(glm::u64 vertexCount, glm::u32 instanceCount, glm::u32 firstVertex, glm::u32 firstInstance) override;
        kor::CommandBuffer& DrawIndexed(glm::u64 indexCount, glm::u32 instanceCount, glm::u32 firstIndex, glm::i32 vertexOffset, glm::u32 firstInstance) override;
        kor::CommandBuffer& doDrawIndirect(kor::ResourceRef<const kor::Buffer> indirectBuffer, glm::u64 offset, glm::u32 drawCount, glm::u32 stride) override;
        kor::CommandBuffer& doDrawIndexedIndirect(kor::ResourceRef<const kor::Buffer> indirectBuffer, glm::u64 offset, glm::u32 drawCount, glm::u32 stride) override;

        kor::CommandBuffer& SetViewport(glm::u32 x, glm::u32 y, glm::u32 width, glm::u32 height) override;
        kor::CommandBuffer& SetScissor(glm::u32 x, glm::u32 y, glm::u32 width, glm::u32 height) override;

        // Dynamic-state overrides. GL applies the pipeline defaults during Bind and
        // replays commands in order, so an override just records the matching GL call
        // after the bind.
        kor::CommandBuffer& SetLineWidth(float lineWidth) override;
        kor::CommandBuffer& SetDepthBias(float constantFactor, float clamp, float slopeFactor) override;
        kor::CommandBuffer& SetBlendConstants(glm::vec4 constants) override;
        kor::CommandBuffer& SetStencilCompareMask(StencilFace face, glm::u32 compareMask) override;
        kor::CommandBuffer& SetStencilWriteMask(StencilFace face, glm::u32 writeMask) override;
        kor::CommandBuffer& SetStencilReference(StencilFace face, glm::u32 reference) override;
        kor::CommandBuffer& SetCullMode(Flags<CullMode> cullMode) override;
        kor::CommandBuffer& SetFrontFace(FrontFace frontFace) override;
        kor::CommandBuffer& SetDepthTestEnable(bool enable) override;
        kor::CommandBuffer& SetDepthWriteEnable(bool enable) override;
        kor::CommandBuffer& SetDepthCompareOp(CompareOp compareOp) override;
        kor::CommandBuffer& SetStencilTestEnable(bool enable) override;
        kor::CommandBuffer& SetStencilOp(StencilFace face, StencilOp failOp, StencilOp passOp, StencilOp depthFailOp, CompareOp compareOp) override;
        kor::CommandBuffer& SetDepthBiasEnable(bool enable) override;
        kor::CommandBuffer& SetRasterizerDiscardEnable(bool enable) override;
        kor::CommandBuffer& SetPrimitiveRestartEnable(bool enable) override;

        kor::CommandBuffer& doBlit(ResourceRef<const Image> srcImage, kor::Blit blitInfo) override;
        kor::CommandBuffer& doBlit(kor::ResourceRef<const Image> srcImage, kor::ResourceRef<const Image> dstImage, kor::Blit blitInfo) override;
        kor::CommandBuffer& doGenerateMipmaps(kor::ResourceRef<const kor::Image> image) override;
        kor::CommandBuffer& doResolve(ResourceRef<const Image> srcImage, kor::Resolve resolveInfo) override;
        kor::CommandBuffer& doResolve(kor::ResourceRef<const Image> srcImage, kor::ResourceRef<const Image> dstImage, kor::Resolve resolveInfo) override;

        kor::CommandBuffer& doClearBuffer(kor::ResourceRef<const kor::Buffer> buffer, glm::u64 offset, glm::u64 size) override;
        kor::CommandBuffer& doClearColorImage(kor::ResourceRef<const kor::Image> image, glm::vec4 color) override;
        kor::CommandBuffer& doFillBuffer(kor::ResourceRef<const kor::Buffer> buffer, void *data, glm::u64 offset, glm::u64 size) override;
        kor::CommandBuffer& doCopyBuffer(ResourceRef<const kor::Buffer> srcBuffer, ResourceRef<const kor::Buffer> dstBuffer, glm::u64 size, glm::u64 srcOffset, glm::u64 dstOffset) override;

        kor::CommandBuffer& Run(const std::function<void(kor::CommandBuffer&)>& command) override;

        kor::CommandBuffer& doCopyBufferToImage(ResourceRef<const kor::Buffer> buffer, ResourceRef<const kor::Image> image, kor::Copy copyInfo) override;
        kor::CommandBuffer& doCopyImageToBuffer(ResourceRef<const kor::Image> image, ResourceRef<const kor::Buffer> buffer, kor::Copy copyInfo) override;

        kor::VoidResult Submit() override;
        void Reset() override;

        const std::map<std::pair<glm::u32, glm::u32>, glm::u32>& getRemappingTableForBoundPipeline() const;

        // Bound-pipeline accessors used by DescriptorSet::bind to drive bindless
        // material arrays (see BindlessSamplerArray): the linked program object and
        // the bindless arrays the pipeline declared.
        [[nodiscard]] GLuint getBoundPipelineProgram() const;
        [[nodiscard]] const std::vector<BindlessSamplerArray>& getBoundPipelineBindlessArrays() const;

        void WaitForFence() const override {}

    protected:
        kor::CommandBuffer & PushConstants(const void *data, glm::u32 size, glm::u32 offset) override;

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

