//
// Created by radue on 2/27/2026.
//

#pragma once

#include <functional>
#include "vk_wrapper.h"
#include <vulkan/vulkan.hpp>

#include <commandBuffer.h>

namespace kor::vk
{
    class Queue;

    class CommandBuffer : public kor::CommandBuffer, public kor::vk::Wrapper<::vk::CommandBuffer> {
    public:
        /// @p fence and @p timerPool come from a recycled command buffer; null makes new ones.
        CommandBuffer(const kor::vk::Queue& queue, ::vk::CommandBuffer commandBuffer, ::vk::CommandPool parentCommandPool,
                      ::vk::Fence fence = nullptr, ::vk::QueryPool timerPool = nullptr);
        ~CommandBuffer() override;
        void Run(const std::function<void(const kor::vk::CommandBuffer&)>& command, ::vk::Semaphore waitSemaphore = nullptr) const;

        [[nodiscard]] ::vk::CommandPool getParentPool() const { return _parentPool; }
        [[nodiscard]] const ::vk::Fence& getFence() const { return _fence; }
        [[nodiscard]] ::vk::QueryPool getTimerPool() const { return _timerPool; }
        [[nodiscard]] const kor::vk::Queue& getQueue() const { return _queue; }

        kor::CommandBuffer& DoBegin() override;
        void DoEnd() override;
        kor::CommandBuffer& DoBeginRendering(const RenderInfo& renderInfo) override;
        kor::CommandBuffer& DoEndRendering() override;
        kor::CommandBuffer& DoSetViewport(glm::u32 x, glm::u32 y, glm::u32 width, glm::u32 height) override;
        kor::CommandBuffer& DoSetScissor(glm::u32 x, glm::u32 y, glm::u32 width, glm::u32 height) override;
        kor::CommandBuffer& DoSetLineWidth(float lineWidth) override;
        kor::CommandBuffer& DoSetDepthBias(float constantFactor, float clamp, float slopeFactor) override;
        kor::CommandBuffer& DoSetBlendConstants(glm::vec4 constants) override;
        kor::CommandBuffer& DoSetStencilCompareMask(StencilFace face, glm::u32 compareMask) override;
        kor::CommandBuffer& DoSetStencilWriteMask(StencilFace face, glm::u32 writeMask) override;
        kor::CommandBuffer& DoSetStencilReference(StencilFace face, glm::u32 reference) override;
        kor::CommandBuffer& DoSetCullMode(Flags<CullMode> cullMode) override;
        kor::CommandBuffer& DoSetFrontFace(FrontFace frontFace) override;
        kor::CommandBuffer& DoSetDepthTestEnable(bool enable) override;
        kor::CommandBuffer& DoSetDepthWriteEnable(bool enable) override;
        kor::CommandBuffer& DoSetDepthCompareOp(CompareOp compareOp) override;
        kor::CommandBuffer& DoSetStencilTestEnable(bool enable) override;
        kor::CommandBuffer& DoSetStencilOp(StencilFace face, StencilOp failOp, StencilOp passOp, StencilOp depthFailOp, CompareOp compareOp) override;
        kor::CommandBuffer& DoSetDepthBiasEnable(bool enable) override;
        kor::CommandBuffer& DoSetRasterizerDiscardEnable(bool enable) override;
        kor::CommandBuffer& DoSetPrimitiveRestartEnable(bool enable) override;
        kor::CommandBuffer& DoBindComputePipeline(kor::ResourceRef<const kor::ComputePipeline> pipeline) override;
        kor::CommandBuffer& DoBindGraphicsPipeline(kor::ResourceRef<const kor::GraphicsPipeline> pipeline) override;
        kor::CommandBuffer& DoBindRayTracingPipeline(kor::ResourceRef<const kor::RayTracingPipeline> pipeline) override;
        kor::CommandBuffer& DoBindDescriptorSet(glm::u32 index, kor::ResourceRef<const kor::DescriptorSet> set) override;
        kor::CommandBuffer& DoBindMesh(kor::ResourceRef<const Mesh> mesh) override;
        kor::CommandBuffer& DoBindVertexBuffer(glm::u32 binding, kor::ResourceRef<const kor::Buffer> buffer, glm::u64 offset) override;
        kor::CommandBuffer& DoBarrier(std::vector<kor::BufferBarrier> bufferBarriers, std::vector<kor::ImageBarrier> imageBarriers) override;
        kor::CommandBuffer& DoBeginDebugLabel(const std::string& label, glm::vec4 color) override;
        kor::CommandBuffer& DoEndDebugLabel() override;
        kor::CommandBuffer& DoInsertDebugLabel(const std::string& label, glm::vec4 color) override;
        kor::CommandBuffer& DoDispatch(glm::u32 groupCountX, glm::u32 groupCountY, glm::u32 groupCountZ, std::source_location where) override;
        kor::CommandBuffer& DoDispatchIndirect(kor::ResourceRef<const kor::Buffer> indirectBuffer, glm::u64 offset) override;
        kor::CommandBuffer& DoTraceRays(glm::u32 width, glm::u32 height, glm::u32 depth, std::source_location where) override;
        kor::CommandBuffer& DoDraw(glm::u64 vertexCount, glm::u32 instanceCount, glm::u32 firstVertex, glm::u32 firstInstance, std::source_location where) override;
        kor::CommandBuffer& DoDrawIndexed(glm::u64 indexCount, glm::u32 instanceCount, glm::u32 firstIndex, glm::i32 vertexOffset, glm::u32 firstInstance, std::source_location where) override;
        kor::CommandBuffer& DoDrawMeshTasks(glm::u32 taskCountX, glm::u32 taskCountY, glm::u32 taskCountZ, std::source_location where) override;
        kor::CommandBuffer& DoDrawIndirect(kor::ResourceRef<const kor::Buffer> indirectBuffer, glm::u64 offset, glm::u32 drawCount, glm::u32 stride) override;
        kor::CommandBuffer& DoDrawIndexedIndirect(kor::ResourceRef<const kor::Buffer> indirectBuffer, glm::u64 offset, glm::u32 drawCount, glm::u32 stride) override;
        kor::CommandBuffer& DoDrawMeshTasksIndirect(kor::ResourceRef<const kor::Buffer> indirectBuffer, glm::u64 offset, glm::u32 drawCount, glm::u32 stride) override;

        kor::CommandBuffer& DoClearBuffer(kor::ResourceRef<const kor::Buffer> buffer, glm::u64 offset, glm::u64 size) override;
        kor::CommandBuffer& DoClearColorImage(kor::ResourceRef<const kor::Image> image, glm::vec4 color) override;
        kor::CommandBuffer& DoFillBuffer(kor::ResourceRef<const kor::Buffer> buffer, const void* data, glm::u64 offset, glm::u64 size) override;
        kor::CommandBuffer& DoCopyBuffer(ResourceRef<const kor::Buffer> srcBuffer, ResourceRef<const kor::Buffer> dstBuffer, glm::u64 size, glm::u64 srcOffset, glm::u64 dstOffset) override;

        kor::CommandBuffer& DoBlitToScreen(ResourceRef<const Image> srcImage, kor::Blit blitInfo) override;
        kor::CommandBuffer& DoBlit(kor::ResourceRef<const kor::Image> srcImage, kor::ResourceRef<const kor::Image> dstImage, kor::Blit blitInfo) override;
        kor::CommandBuffer& DoCopyImage(kor::ResourceRef<const kor::Image> srcImage, kor::ResourceRef<const kor::Image> dstImage) override;
        kor::CommandBuffer& DoResolveToScreen(ResourceRef<const Image> srcImage, kor::Resolve resolveInfo) override;
        kor::CommandBuffer& DoResolve(kor::ResourceRef<const Image> srcImage, kor::ResourceRef<const Image> dstImage, kor::Resolve resolveInfo) override;

        kor::CommandBuffer& DoCopyBufferToImage(ResourceRef<const kor::Buffer> buffer, ResourceRef<const kor::Image> image, kor::Copy copyInfo) override;
        kor::CommandBuffer& DoCopyImageToBuffer(ResourceRef<const kor::Image> image, ResourceRef<const kor::Buffer> buffer, kor::Copy copyInfo) override;

        kor::CommandBuffer& DoRun(const std::function<void(kor::CommandBuffer&)>& command) override;

        kor::VoidResult DoSubmit(const kor::SubmitInfo& info) override;
        void DoReset() override;

        void DoWaitForFence() const override;

        [[nodiscard]] bool DoSupportsTimers() const override { return _timestampPeriod > 0.f; }

    private:
        // Park an emit closure as a core Record. Every override that talks to _handle goes
        // through this: the base class has already validated and advanced its tracked state by
        // the time we get here, and what remains is the GPU call itself, which must not happen
        // until End() has worked out where the barriers belong.
        // As defer(), but with the *caller's* source location rather than this header's.
        // The device-address diagnostic quotes the file and line of the command it blames, so
        // for anything that can appear in that report the location has to come from the user's
        // call site, not from wherever the emit closure happened to be parked.
        template<typename F>
        kor::CommandBuffer& deferAt(const char* name, const std::source_location where, F&& emit,
                                    const PassEdge pass = PassEdge::eNone,
                                    std::vector<ResourceUse> uses = {},
                                    const bool dereferencesDeviceAddresses = false) {
            return Enqueue(name, where, std::move(uses), pass, std::forward<F>(emit),
                           /*transitions=*/false, dereferencesDeviceAddresses);
        }

        template<typename F>
        kor::CommandBuffer& defer(const char* name, F&& emit,
                                  const PassEdge pass = PassEdge::eNone,
                                  std::vector<ResourceUse> uses = {},
                                  const bool dereferencesDeviceAddresses = false) {
            return Enqueue(name, std::source_location::current(), std::move(uses), pass,
                           std::forward<F>(emit), /*transitions=*/false, dereferencesDeviceAddresses);
        }

    protected:
        kor::CommandBuffer & DoPushConstantBlock(const void *data, glm::u32 size, glm::u32 offset) override;
        kor::Resource<kor::Image> _resolveHelperImage;

        void DoWriteTimerTimestamp(glm::u32 queryIndex) override;
        bool DoReadTimerTimestamps(glm::u32 scopeCount, std::vector<double>& millisecondsOut) override;

    private:
        const kor::vk::Queue& _queue;
        ::vk::CommandPool _parentPool; // this buffer's own; see Device::requestCommandBuffer
        // The tokens the last submission waits on or signals. Their timeline semaphores must outlive
        // it, and a caller dropping its token straight after Submit() is the normal fire-and-forget
        // case, so the buffer holds them until it is re-recorded — the point where the timers above
        // also conclude the GPU is done with it.
        std::vector<kor::Token> _inFlight;
        // Completion is reported by the fence alone. There was a semaphore signalled alongside it
        // here, which nothing ever waited on — and a binary semaphore signalled twice with no wait
        // in between is invalid, so re-submitting the same buffer tripped validation for nothing.
        // Ordering against other submissions belongs to the scheduler, which carries its own.
        ::vk::Fence _fence = nullptr;
        // Whether a submission carrying the fence has not been waited out yet. Its status alone cannot
        // say: the fence lands after the timeline semaphores do (on MoltenVK noticeably so), so a
        // buffer whose tokens are all done can still have an unsignalled fence that signals later —
        // after being reset, recycled and handed to the next submit, which then trips VUID-00063.
        mutable bool _fencePending = false;

        // One pool for the whole command buffer, two queries per timer scope. Sized up front
        // because a Vulkan query pool cannot grow, and reset in its entirety at Begin() — the only
        // point in the stream guaranteed to be outside a render pass, which vkCmdResetQueryPool
        // requires.
        ::vk::QueryPool _timerPool = nullptr;
        // Nanoseconds per timestamp tick, or 0 when this queue cannot timestamp at all, which is
        // what SupportsTimers() reads.
        float _timestampPeriod = 0.f;
    };
}
