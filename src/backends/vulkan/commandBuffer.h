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
        CommandBuffer(const kor::vk::Queue& queue, ::vk::CommandBuffer commandBuffer, const ::vk::CommandPool& parentCommandPool);
        ~CommandBuffer() override;
        void Run(const std::function<void(const kor::vk::CommandBuffer&)>& command, ::vk::Semaphore waitSemaphore = nullptr) const;

        [[nodiscard]] const ::vk::CommandPool& getParentPool() const { return _parentPool; }
        [[nodiscard]] const ::vk::Fence& getFence() const { return _fence; }
        [[nodiscard]] const kor::vk::Queue& getQueue() const { return _queue; }

        kor::CommandBuffer& doBegin() override;
        void doEnd() override;
        kor::CommandBuffer& doBeginRendering(const RenderInfo& renderInfo) override;
        kor::CommandBuffer& doEndRendering() override;
        kor::CommandBuffer& doSetViewport(glm::u32 x, glm::u32 y, glm::u32 width, glm::u32 height) override;
        kor::CommandBuffer& doSetScissor(glm::u32 x, glm::u32 y, glm::u32 width, glm::u32 height) override;
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
        kor::CommandBuffer& doBindComputePipeline(kor::ResourceRef<const kor::ComputePipeline> pipeline) override;
        kor::CommandBuffer& doBindGraphicsPipeline(kor::ResourceRef<const kor::GraphicsPipeline> pipeline) override;
        kor::CommandBuffer& doBindRayTracingPipeline(kor::ResourceRef<const kor::RayTracingPipeline> pipeline) override;
        kor::CommandBuffer& doBindDescriptorSet(glm::u32 index, kor::ResourceRef<const kor::DescriptorSet> set) override;
        kor::CommandBuffer& doBindMesh(kor::ResourceRef<const Mesh> mesh) override;
        kor::CommandBuffer& doBarrier(std::vector<kor::BufferBarrier> bufferBarriers, std::vector<kor::ImageBarrier> imageBarriers) override;
        kor::CommandBuffer& doBeginDebugLabel(const std::string& label, glm::vec4 color) override;
        kor::CommandBuffer& doEndDebugLabel() override;
        kor::CommandBuffer& doInsertDebugLabel(const std::string& label, glm::vec4 color) override;
        kor::CommandBuffer& doDispatch(glm::u32 groupCountX, glm::u32 groupCountY, glm::u32 groupCountZ, std::source_location where) override;
        kor::CommandBuffer& doDispatchIndirect(kor::ResourceRef<const kor::Buffer> indirectBuffer, glm::u64 offset) override;
        kor::CommandBuffer& doTraceRays(glm::u32 width, glm::u32 height, glm::u32 depth, std::source_location where) override;
        kor::CommandBuffer& doDraw(glm::u64 vertexCount, glm::u32 instanceCount, glm::u32 firstVertex, glm::u32 firstInstance, std::source_location where) override;
        kor::CommandBuffer& doDrawIndexed(glm::u64 indexCount, glm::u32 instanceCount, glm::u32 firstIndex, glm::i32 vertexOffset, glm::u32 firstInstance, std::source_location where) override;
        kor::CommandBuffer& doDrawMeshTasks(glm::u32 taskCountX, glm::u32 taskCountY, glm::u32 taskCountZ, std::source_location where) override;
        kor::CommandBuffer& doDrawIndirect(kor::ResourceRef<const kor::Buffer> indirectBuffer, glm::u64 offset, glm::u32 drawCount, glm::u32 stride) override;
        kor::CommandBuffer& doDrawIndexedIndirect(kor::ResourceRef<const kor::Buffer> indirectBuffer, glm::u64 offset, glm::u32 drawCount, glm::u32 stride) override;
        kor::CommandBuffer& doDrawMeshTasksIndirect(kor::ResourceRef<const kor::Buffer> indirectBuffer, glm::u64 offset, glm::u32 drawCount, glm::u32 stride) override;

        kor::CommandBuffer& doClearBuffer(kor::ResourceRef<const kor::Buffer> buffer, glm::u64 offset, glm::u64 size) override;
        kor::CommandBuffer& doClearColorImage(kor::ResourceRef<const kor::Image> image, glm::vec4 color) override;
        kor::CommandBuffer& doFillBuffer(kor::ResourceRef<const kor::Buffer> buffer, const void* data, glm::u64 offset, glm::u64 size) override;
        kor::CommandBuffer& doCopyBuffer(ResourceRef<const kor::Buffer> srcBuffer, ResourceRef<const kor::Buffer> dstBuffer, glm::u64 size, glm::u64 srcOffset, glm::u64 dstOffset) override;

        kor::CommandBuffer& doBlitToScreen(ResourceRef<const Image> srcImage, kor::Blit blitInfo) override;
        kor::CommandBuffer& doBlit(kor::ResourceRef<const kor::Image> srcImage, kor::ResourceRef<const kor::Image> dstImage, kor::Blit blitInfo) override;
        kor::CommandBuffer& doResolveToScreen(ResourceRef<const Image> srcImage, kor::Resolve resolveInfo) override;
        kor::CommandBuffer& doResolve(kor::ResourceRef<const Image> srcImage, kor::ResourceRef<const Image> dstImage, kor::Resolve resolveInfo) override;

        kor::CommandBuffer& doCopyBufferToImage(ResourceRef<const kor::Buffer> buffer, ResourceRef<const kor::Image> image, kor::Copy copyInfo) override;
        kor::CommandBuffer& doCopyImageToBuffer(ResourceRef<const kor::Image> image, ResourceRef<const kor::Buffer> buffer, kor::Copy copyInfo) override;

        kor::CommandBuffer& doRun(const std::function<void(kor::CommandBuffer&)>& command) override;

        kor::VoidResult doSubmit() override;
        void doReset() override;

        void doWaitForFence() const override;

        [[nodiscard]] bool doSupportsTimers() const override { return _timestampPeriod > 0.f; }

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
            return enqueue(name, where, std::move(uses), pass, std::forward<F>(emit),
                           /*transitions=*/false, dereferencesDeviceAddresses);
        }

        template<typename F>
        kor::CommandBuffer& defer(const char* name, F&& emit,
                                  const PassEdge pass = PassEdge::eNone,
                                  std::vector<ResourceUse> uses = {},
                                  const bool dereferencesDeviceAddresses = false) {
            return enqueue(name, std::source_location::current(), std::move(uses), pass,
                           std::forward<F>(emit), /*transitions=*/false, dereferencesDeviceAddresses);
        }

    protected:
        kor::CommandBuffer & doPushConstantBlock(const void *data, glm::u32 size, glm::u32 offset) override;
        kor::Resource<kor::Image> _resolveHelperImage;

        void doWriteTimerTimestamp(glm::u32 queryIndex) override;
        bool doReadTimerTimestamps(glm::u32 scopeCount, std::vector<double>& millisecondsOut) override;

    private:
        const kor::vk::Queue& _queue;
        const ::vk::CommandPool& _parentPool;
        // Completion is reported by the fence alone. There was a semaphore signalled alongside it
        // here, which nothing ever waited on — and a binary semaphore signalled twice with no wait
        // in between is invalid, so re-submitting the same buffer tripped validation for nothing.
        // Ordering against other submissions belongs to the scheduler, which carries its own.
        ::vk::Fence _fence = nullptr;

        // One pool for the whole command buffer, two queries per timer scope. Sized up front
        // because a Vulkan query pool cannot grow, and reset in its entirety at Begin() — the only
        // point in the stream guaranteed to be outside a render pass, which vkCmdResetQueryPool
        // requires.
        ::vk::QueryPool _timerPool = nullptr;
        // Nanoseconds per timestamp tick, or 0 when this queue cannot timestamp at all, which is
        // what supportsTimers() reads.
        float _timestampPeriod = 0.f;
    };
}
