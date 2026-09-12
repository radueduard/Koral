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
        ~CommandBuffer() override;

        kor::CommandBuffer& doBegin() override;
        void doEnd() override;

        kor::CommandBuffer& doBeginRendering(const RenderInfo& renderParameters) override;
        kor::CommandBuffer& doEndRendering() override;
        kor::CommandBuffer& doBindComputePipeline(kor::ResourceRef<const kor::ComputePipeline> pipeline) override;
        kor::CommandBuffer& doBindGraphicsPipeline(kor::ResourceRef<const kor::GraphicsPipeline> pipeline) override;
        kor::CommandBuffer& doBindDescriptorSet(glm::u32 index, kor::ResourceRef<const kor::DescriptorSet> set) override;
        kor::CommandBuffer& doBindMesh(kor::ResourceRef<const Mesh> mesh) override;
        kor::CommandBuffer& doBarrier(std::vector<kor::BufferBarrier> bufferBarriers, std::vector<kor::ImageBarrier> imageBarriers) override;
        kor::CommandBuffer& doBeginDebugLabel(const std::string& label, glm::vec4 color) override;
        kor::CommandBuffer& doEndDebugLabel() override;
        kor::CommandBuffer& doInsertDebugLabel(const std::string& label, glm::vec4 color) override;
        kor::CommandBuffer& doDispatch(glm::u32 groupCountX, glm::u32 groupCountY, glm::u32 groupCountZ, std::source_location where) override;
        kor::CommandBuffer& doDispatchIndirect(kor::ResourceRef<const kor::Buffer> indirectBuffer, glm::u64 offset) override;
        kor::CommandBuffer& doDraw(glm::u64 vertexCount, glm::u32 instanceCount, glm::u32 firstVertex, glm::u32 firstInstance, std::source_location where) override;
        kor::CommandBuffer& doDrawIndexed(glm::u64 indexCount, glm::u32 instanceCount, glm::u32 firstIndex, glm::i32 vertexOffset, glm::u32 firstInstance, std::source_location where) override;
        kor::CommandBuffer& doDrawIndirect(kor::ResourceRef<const kor::Buffer> indirectBuffer, glm::u64 offset, glm::u32 drawCount, glm::u32 stride) override;
        kor::CommandBuffer& doDrawIndexedIndirect(kor::ResourceRef<const kor::Buffer> indirectBuffer, glm::u64 offset, glm::u32 drawCount, glm::u32 stride) override;

        kor::CommandBuffer& doSetViewport(glm::u32 x, glm::u32 y, glm::u32 width, glm::u32 height) override;
        kor::CommandBuffer& doSetScissor(glm::u32 x, glm::u32 y, glm::u32 width, glm::u32 height) override;

        // Dynamic-state overrides. GL applies the pipeline defaults during Bind and
        // replays commands in order, so an override just records the matching GL call
        // after the bind.
        kor::CommandBuffer& doSetLineWidth(float lineWidth) override;
        kor::CommandBuffer& doSetDepthBias(float constantFactor, float clamp, float slopeFactor) override;
        kor::CommandBuffer& doSetBlendConstants(glm::vec4 constants) override;
        kor::CommandBuffer& doSetStencilCompareMask(StencilFace face, glm::u32 compareMask) override;
        kor::CommandBuffer& doSetStencilWriteMask(StencilFace face, glm::u32 writeMask) override;
        kor::CommandBuffer& doSetStencilReference(StencilFace face, glm::u32 reference) override;
        kor::CommandBuffer& doSetCullMode(Flags<CullMode> cullMode) override;
        kor::CommandBuffer& doSetFrontFace(FrontFace frontFace) override;
        kor::CommandBuffer& doSetDepthTestEnable(bool enable) override;
        kor::CommandBuffer& doSetDepthWriteEnable(bool enable) override;
        kor::CommandBuffer& doSetDepthCompareOp(CompareOp compareOp) override;
        kor::CommandBuffer& doSetStencilTestEnable(bool enable) override;
        kor::CommandBuffer& doSetStencilOp(StencilFace face, StencilOp failOp, StencilOp passOp, StencilOp depthFailOp, CompareOp compareOp) override;
        kor::CommandBuffer& doSetDepthBiasEnable(bool enable) override;
        kor::CommandBuffer& doSetRasterizerDiscardEnable(bool enable) override;
        kor::CommandBuffer& doSetPrimitiveRestartEnable(bool enable) override;

        kor::CommandBuffer& doBlitToScreen(ResourceRef<const Image> srcImage, kor::Blit blitInfo) override;
        kor::CommandBuffer& doBlit(kor::ResourceRef<const Image> srcImage, kor::ResourceRef<const Image> dstImage, kor::Blit blitInfo) override;
        kor::CommandBuffer& doGenerateMipmaps(kor::ResourceRef<const kor::Image> image) override;
        kor::CommandBuffer& doResolveToScreen(ResourceRef<const Image> srcImage, kor::Resolve resolveInfo) override;
        kor::CommandBuffer& doResolve(kor::ResourceRef<const Image> srcImage, kor::ResourceRef<const Image> dstImage, kor::Resolve resolveInfo) override;

        kor::CommandBuffer& doClearBuffer(kor::ResourceRef<const kor::Buffer> buffer, glm::u64 offset, glm::u64 size) override;
        kor::CommandBuffer& doClearColorImage(kor::ResourceRef<const kor::Image> image, glm::vec4 color) override;
        kor::CommandBuffer& doFillBuffer(kor::ResourceRef<const kor::Buffer> buffer, const void* data, glm::u64 offset, glm::u64 size) override;
        kor::CommandBuffer& doCopyBuffer(ResourceRef<const kor::Buffer> srcBuffer, ResourceRef<const kor::Buffer> dstBuffer, glm::u64 size, glm::u64 srcOffset, glm::u64 dstOffset) override;

        kor::CommandBuffer& doRun(const std::function<void(kor::CommandBuffer&)>& command) override;

        kor::CommandBuffer& doCopyBufferToImage(ResourceRef<const kor::Buffer> buffer, ResourceRef<const kor::Image> image, kor::Copy copyInfo) override;
        kor::CommandBuffer& doCopyImageToBuffer(ResourceRef<const kor::Image> image, ResourceRef<const kor::Buffer> buffer, kor::Copy copyInfo) override;

        kor::VoidResult doSubmit() override;
        void doReset() override;

        const std::map<std::pair<glm::u32, glm::u32>, glm::u32>& getRemappingTableForBoundPipeline() const;

        // Bound-pipeline accessors used by DescriptorSet::bind to drive bindless
        // material arrays (see BindlessSamplerArray): the linked program object and
        // the bindless arrays the pipeline declared.
        [[nodiscard]] GLuint getBoundPipelineProgram() const;
        [[nodiscard]] const std::vector<BindlessSamplerArray>& getBoundPipelineBindlessArrays() const;

        // glFinish, not nothing. The contract is "blocks until the GPU has finished the work
        // submitted from this buffer", and callers rely on it: a readback expects its data to be
        // there, and collectTimer expects the timestamps to have landed. GL mostly got away with a
        // no-op because a following readback synchronises implicitly — a query poll does not.
        void doWaitForFence() const override;

        // glQueryCounter is core in 3.3 and the scheduler already refuses to start below 4.5, so
        // there is no device here that cannot be timed.
        [[nodiscard]] bool doSupportsTimers() const override { return true; }

    protected:
        kor::CommandBuffer & doPushConstantBlock(const void *data, glm::u32 size, glm::u32 offset) override;

        void doWriteTimerTimestamp(glm::u32 queryIndex) override;
        bool doReadTimerTimestamps(glm::u32 scopeCount, std::vector<double>& millisecondsOut) override;

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
        // Hand the operation to the core's record list rather than keeping a second one.
        // Everything the core records — barriers the resolver inserted, and the commands whose
        // gate lives in the base class — has to interleave with these in call order, which only
        // works if there is a single list. The core's enqueue keeps the reentrancy behaviour
        // this used to implement locally: recorded from inside a replay, it runs in place.
        void enqueue(std::function<void()> op) {
            kor::CommandBuffer::enqueue("", std::source_location::current(), {}, PassEdge::eNone, std::move(op));
        }

        bool _recording = false;
        bool _filled = false;
        bool _submitted = false;

        // Two GL query objects per timer scope, grown on demand and then reused for the life of
        // the command buffer. Unlike Vulkan's pool these need no reset: glQueryCounter overwrites
        // the object's result outright.
        std::vector<GLuint> _timerQueries;
    };
}

