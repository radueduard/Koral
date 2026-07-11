//
// Created by radue on 2/27/2026.
//

#pragma once

#include <functional>
#include "vk_wrapper.h"
#include <vulkan/vulkan.hpp>

#include <commandBuffer.h>

namespace gfx::vk
{
    class Queue;

    class CommandBuffer : public gfx::CommandBuffer, public gfx::vk::Wrapper<::vk::CommandBuffer> {
    public:
        CommandBuffer(const gfx::vk::Queue& queue, ::vk::CommandBuffer commandBuffer, const ::vk::CommandPool& parentCommandPool);
        ~CommandBuffer() override;
        void Run(const std::function<void(const gfx::vk::CommandBuffer&)>& command, ::vk::Semaphore waitSemaphore = nullptr) const;

        [[nodiscard]] const ::vk::CommandPool& getParentPool() const { return _parentPool; }
        [[nodiscard]] const ::vk::Semaphore& getSignalSemaphore() const { return _signalSemaphore; }
        [[nodiscard]] const ::vk::Fence& getFence() const { return _fence; }
        [[nodiscard]] const gfx::vk::Queue& getQueue() const { return _queue; }

        gfx::CommandBuffer& Begin() override;
        void End() override;
        gfx::CommandBuffer& BeginRendering(RenderParameters renderParameters) override;
        gfx::CommandBuffer& doBeginRendering(gfx::ResourceRef<const gfx::Framebuffer> framebuffer, RenderParameters renderParameters) override;
        gfx::CommandBuffer& EndRendering() override;
        gfx::CommandBuffer& SetViewport(glm::u32 x, glm::u32 y, glm::u32 width, glm::u32 height) override;
        gfx::CommandBuffer& SetScissor(glm::u32 x, glm::u32 y, glm::u32 width, glm::u32 height) override;
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
        gfx::CommandBuffer& doBindComputePipeline(gfx::ResourceRef<const gfx::ComputePipeline> pipeline) override;
        gfx::CommandBuffer& doBindGraphicsPipeline(gfx::ResourceRef<const gfx::GraphicsPipeline> pipeline) override;
        gfx::CommandBuffer& doBindRayTracingPipeline(gfx::ResourceRef<const gfx::RayTracingPipeline> pipeline) override;
        gfx::CommandBuffer& doBindDescriptorSet(glm::u32 index, gfx::ResourceRef<const gfx::DescriptorSet> set, bool debug) override;
        gfx::CommandBuffer& doBindMesh(gfx::ResourceRef<const Mesh> mesh) override;
        gfx::CommandBuffer& doBarrier(std::vector<gfx::BufferBarrier> bufferBarriers, std::vector<gfx::ImageBarrier> imageBarriers) override;
        gfx::CommandBuffer& BeginDebugLabel(const std::string& label, glm::vec4 color) override;
        gfx::CommandBuffer& EndDebugLabel() override;
        gfx::CommandBuffer& InsertDebugLabel(const std::string& label, glm::vec4 color) override;
        gfx::CommandBuffer& Dispatch(glm::u32 groupCountX, glm::u32 groupCountY, glm::u32 groupCountZ) override;
        gfx::CommandBuffer& doDispatchIndirect(gfx::ResourceRef<const gfx::Buffer> indirectBuffer, glm::u64 offset) override;
        gfx::CommandBuffer& TraceRays(glm::u32 width, glm::u32 height, glm::u32 depth) override;
        gfx::CommandBuffer& Draw(glm::u64 vertexCount, glm::u32 instanceCount, glm::u32 firstVertex, glm::u32 firstInstance) override;
        gfx::CommandBuffer& DrawIndexed(glm::u64 indexCount, glm::u32 instanceCount, glm::u32 firstIndex, glm::i32 vertexOffset, glm::u32 firstInstance) override;
        gfx::CommandBuffer& DrawMeshTasks(glm::u32 taskCountX, glm::u32 taskCountY, glm::u32 taskCountZ) override;
        gfx::CommandBuffer& doDrawIndirect(gfx::ResourceRef<const gfx::Buffer> indirectBuffer, glm::u64 offset, glm::u32 drawCount, glm::u32 stride) override;
        gfx::CommandBuffer& doDrawIndexedIndirect(gfx::ResourceRef<const gfx::Buffer> indirectBuffer, glm::u64 offset, glm::u32 drawCount, glm::u32 stride) override;
        gfx::CommandBuffer& doDrawMeshTasksIndirect(gfx::ResourceRef<const gfx::Buffer> indirectBuffer, glm::u64 offset, glm::u32 drawCount, glm::u32 stride) override;

        gfx::CommandBuffer& doClearBuffer(gfx::ResourceRef<const gfx::Buffer> buffer, glm::u64 offset, glm::u64 size) override;
        gfx::CommandBuffer& doClearColorImage(gfx::ResourceRef<const gfx::Image> image, glm::vec4 color) override;
        gfx::CommandBuffer& doFillBuffer(gfx::ResourceRef<const gfx::Buffer> buffer, void *data, glm::u64 offset, glm::u64 size) override;
        gfx::CommandBuffer& doCopyBuffer(ResourceRef<const gfx::Buffer> srcBuffer, ResourceRef<const gfx::Buffer> dstBuffer, glm::u64 size, glm::u64 srcOffset, glm::u64 dstOffset) override;

        gfx::CommandBuffer& doBlit(ResourceRef<const Image> srcImage, gfx::Blit blitInfo) override;
        gfx::CommandBuffer& doBlit(gfx::ResourceRef<const gfx::Image> srcImage, gfx::ResourceRef<const gfx::Image> dstImage, gfx::Blit blitInfo) override;
        gfx::CommandBuffer& doResolve(ResourceRef<const Image> srcImage, gfx::Resolve resolveInfo) override;
        gfx::CommandBuffer& doResolve(gfx::ResourceRef<const Image> srcImage, gfx::ResourceRef<const Image> dstImage, gfx::Resolve resolveInfo) override;

        gfx::CommandBuffer& doCopyBufferToImage(ResourceRef<const gfx::Buffer> buffer, ResourceRef<const gfx::Image> image, gfx::Copy copyInfo) override;
        gfx::CommandBuffer& doCopyImageToBuffer(ResourceRef<const gfx::Image> image, ResourceRef<const gfx::Buffer> buffer, gfx::Copy copyInfo) override;

        gfx::CommandBuffer& Run(const std::function<void(gfx::CommandBuffer&)>& command) override;

        gfx::VoidResult Submit() override;
        void Reset() override;

        void WaitForFence() const override;

    protected:
        gfx::CommandBuffer & PushConstants(const void *data, glm::u32 size, glm::u32 offset) override;
        gfx::Resource<gfx::Image> _resolveHelperImage;

    private:
        const gfx::vk::Queue& _queue;
        const ::vk::CommandPool& _parentPool;
        ::vk::Semaphore _signalSemaphore = nullptr;
        ::vk::Fence _fence = nullptr;
    };
}
