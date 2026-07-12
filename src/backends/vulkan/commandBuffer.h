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
        [[nodiscard]] const ::vk::Semaphore& getSignalSemaphore() const { return _signalSemaphore; }
        [[nodiscard]] const ::vk::Fence& getFence() const { return _fence; }
        [[nodiscard]] const kor::vk::Queue& getQueue() const { return _queue; }

        kor::CommandBuffer& Begin() override;
        void End() override;
        kor::CommandBuffer& BeginRendering(RenderParameters renderParameters) override;
        kor::CommandBuffer& doBeginRendering(kor::ResourceRef<const kor::Framebuffer> framebuffer, RenderParameters renderParameters) override;
        kor::CommandBuffer& EndRendering() override;
        kor::CommandBuffer& SetViewport(glm::u32 x, glm::u32 y, glm::u32 width, glm::u32 height) override;
        kor::CommandBuffer& SetScissor(glm::u32 x, glm::u32 y, glm::u32 width, glm::u32 height) override;
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
        kor::CommandBuffer& doBindComputePipeline(kor::ResourceRef<const kor::ComputePipeline> pipeline) override;
        kor::CommandBuffer& doBindGraphicsPipeline(kor::ResourceRef<const kor::GraphicsPipeline> pipeline) override;
        kor::CommandBuffer& doBindRayTracingPipeline(kor::ResourceRef<const kor::RayTracingPipeline> pipeline) override;
        kor::CommandBuffer& doBindDescriptorSet(glm::u32 index, kor::ResourceRef<const kor::DescriptorSet> set, bool debug) override;
        kor::CommandBuffer& doBindMesh(kor::ResourceRef<const Mesh> mesh) override;
        kor::CommandBuffer& doBarrier(std::vector<kor::BufferBarrier> bufferBarriers, std::vector<kor::ImageBarrier> imageBarriers) override;
        kor::CommandBuffer& BeginDebugLabel(const std::string& label, glm::vec4 color) override;
        kor::CommandBuffer& EndDebugLabel() override;
        kor::CommandBuffer& InsertDebugLabel(const std::string& label, glm::vec4 color) override;
        kor::CommandBuffer& Dispatch(glm::u32 groupCountX, glm::u32 groupCountY, glm::u32 groupCountZ) override;
        kor::CommandBuffer& doDispatchIndirect(kor::ResourceRef<const kor::Buffer> indirectBuffer, glm::u64 offset) override;
        kor::CommandBuffer& TraceRays(glm::u32 width, glm::u32 height, glm::u32 depth) override;
        kor::CommandBuffer& Draw(glm::u64 vertexCount, glm::u32 instanceCount, glm::u32 firstVertex, glm::u32 firstInstance) override;
        kor::CommandBuffer& DrawIndexed(glm::u64 indexCount, glm::u32 instanceCount, glm::u32 firstIndex, glm::i32 vertexOffset, glm::u32 firstInstance) override;
        kor::CommandBuffer& DrawMeshTasks(glm::u32 taskCountX, glm::u32 taskCountY, glm::u32 taskCountZ) override;
        kor::CommandBuffer& doDrawIndirect(kor::ResourceRef<const kor::Buffer> indirectBuffer, glm::u64 offset, glm::u32 drawCount, glm::u32 stride) override;
        kor::CommandBuffer& doDrawIndexedIndirect(kor::ResourceRef<const kor::Buffer> indirectBuffer, glm::u64 offset, glm::u32 drawCount, glm::u32 stride) override;
        kor::CommandBuffer& doDrawMeshTasksIndirect(kor::ResourceRef<const kor::Buffer> indirectBuffer, glm::u64 offset, glm::u32 drawCount, glm::u32 stride) override;

        kor::CommandBuffer& doClearBuffer(kor::ResourceRef<const kor::Buffer> buffer, glm::u64 offset, glm::u64 size) override;
        kor::CommandBuffer& doClearColorImage(kor::ResourceRef<const kor::Image> image, glm::vec4 color) override;
        kor::CommandBuffer& doFillBuffer(kor::ResourceRef<const kor::Buffer> buffer, void *data, glm::u64 offset, glm::u64 size) override;
        kor::CommandBuffer& doCopyBuffer(ResourceRef<const kor::Buffer> srcBuffer, ResourceRef<const kor::Buffer> dstBuffer, glm::u64 size, glm::u64 srcOffset, glm::u64 dstOffset) override;

        kor::CommandBuffer& doBlit(ResourceRef<const Image> srcImage, kor::Blit blitInfo) override;
        kor::CommandBuffer& doBlit(kor::ResourceRef<const kor::Image> srcImage, kor::ResourceRef<const kor::Image> dstImage, kor::Blit blitInfo) override;
        kor::CommandBuffer& doResolve(ResourceRef<const Image> srcImage, kor::Resolve resolveInfo) override;
        kor::CommandBuffer& doResolve(kor::ResourceRef<const Image> srcImage, kor::ResourceRef<const Image> dstImage, kor::Resolve resolveInfo) override;

        kor::CommandBuffer& doCopyBufferToImage(ResourceRef<const kor::Buffer> buffer, ResourceRef<const kor::Image> image, kor::Copy copyInfo) override;
        kor::CommandBuffer& doCopyImageToBuffer(ResourceRef<const kor::Image> image, ResourceRef<const kor::Buffer> buffer, kor::Copy copyInfo) override;

        kor::CommandBuffer& Run(const std::function<void(kor::CommandBuffer&)>& command) override;

        kor::VoidResult Submit() override;
        void Reset() override;

        void WaitForFence() const override;

    protected:
        kor::CommandBuffer & PushConstants(const void *data, glm::u32 size, glm::u32 offset) override;
        kor::Resource<kor::Image> _resolveHelperImage;

    private:
        const kor::vk::Queue& _queue;
        const ::vk::CommandPool& _parentPool;
        ::vk::Semaphore _signalSemaphore = nullptr;
        ::vk::Fence _fence = nullptr;
    };
}
