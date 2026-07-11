//
// Created by radue on 2/27/2026.
//
#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#include "commandBuffer.h"

#include <ranges>
#include <iostream>
#include <format>

#include "buffer.h"
#include "computePipeline.h"
#include "context.h"
#include "descriptorSet.h"
#include "device.h"
#include "framebuffer.h"
#include "graphicsPipeline.h"
#include "rayTracingPipeline.h"
#include "imageView.h"
#include "scheduler.h"
#include "vulkanContext.h"
#include "vk_enum_conversions.h"

namespace gfx::vk
{
    class ComputePipeline;

    gfx::Flags<CommandBuffer::Usage> getCommandBufferUsage(const gfx::vk::Queue& queue)
    {
        gfx::Flags<CommandBuffer::Usage> usage;
        if (queue.getFamily().getProperties().queueFlags & ::vk::QueueFlagBits::eGraphics)
            usage |= CommandBuffer::Usage::eGraphics;
        if (queue.getFamily().getProperties().queueFlags & ::vk::QueueFlagBits::eCompute)
            usage |= CommandBuffer::Usage::eCompute;
        if (queue.getFamily().getProperties().queueFlags & ::vk::QueueFlagBits::eTransfer)
            usage |= CommandBuffer::Usage::eTransfer;
        return usage;
    }

    CommandBuffer::CommandBuffer(const gfx::vk::Queue& queue, const ::vk::CommandBuffer commandBuffer, const ::vk::CommandPool& parentCommandPool)
        : gfx::CommandBuffer(getCommandBufferUsage(queue)), _queue(queue), _parentPool(parentCommandPool) {
        _handle = commandBuffer;
        _signalSemaphore = Context::Device()->createSemaphore({});
        _fence = gfx::vk::Context::Device()->createFence({});
    }

    CommandBuffer::~CommandBuffer() {
        Context::Device().freeCommandBuffer(*this);

        Context::Device()->destroySemaphore(_signalSemaphore);
        Context::Device()->destroyFence(_fence);
    }

    void CommandBuffer::Run(const std::function<void(const gfx::vk::CommandBuffer&)>& command, ::vk::Semaphore waitSemaphore) const {
        _handle.begin(::vk::CommandBufferBeginInfo().setFlags(::vk::CommandBufferUsageFlagBits::eOneTimeSubmit));
        command(*this);
        _handle.end();

        const auto commandBuffers = std::array { _handle };
        const auto dstStageMask = std::vector<::vk::PipelineStageFlags> { ::vk::PipelineStageFlagBits::eAllCommands };
        auto submitInfo = ::vk::SubmitInfo()
            .setCommandBuffers(commandBuffers);

        if (waitSemaphore != nullptr)
            submitInfo
                .setWaitSemaphores(waitSemaphore)
                .setWaitDstStageMask(dstStageMask);

        if (_signalSemaphore != nullptr)
            submitInfo.setSignalSemaphores(_signalSemaphore);

        try {
            _queue->submit(submitInfo, _fence);
        } catch (const std::runtime_error& e) {
            std::cerr << e.what() << std::endl;
        }
    }

    gfx::CommandBuffer& CommandBuffer::Begin()
    {
        resetErrors();
        constexpr auto commandBufferBeginInfo = ::vk::CommandBufferBeginInfo()
            .setFlags(::vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
        _handle.begin(commandBufferBeginInfo);
        return *this;
    }

    void CommandBuffer::End()
    {
        _handle.end();
    }

    gfx::CommandBuffer& CommandBuffer::BeginDebugLabel(const std::string& label, const glm::vec4 color)
    {
        // Guard on the loaded function pointer: VK_EXT_debug_utils is optional, so the
        // dispatcher entry is null when the instance was created without it.
        if (VULKAN_HPP_DEFAULT_DISPATCHER.vkCmdBeginDebugUtilsLabelEXT) {
            const auto info = ::vk::DebugUtilsLabelEXT()
                .setPLabelName(label.c_str())
                .setColor(std::array<float, 4>{ color.r, color.g, color.b, color.a });
            _handle.beginDebugUtilsLabelEXT(info);
        }
        return *this;
    }

    gfx::CommandBuffer& CommandBuffer::EndDebugLabel()
    {
        if (VULKAN_HPP_DEFAULT_DISPATCHER.vkCmdEndDebugUtilsLabelEXT) {
            _handle.endDebugUtilsLabelEXT();
        }
        return *this;
    }

    gfx::CommandBuffer& CommandBuffer::InsertDebugLabel(const std::string& label, const glm::vec4 color)
    {
        if (VULKAN_HPP_DEFAULT_DISPATCHER.vkCmdInsertDebugUtilsLabelEXT) {
            const auto info = ::vk::DebugUtilsLabelEXT()
                .setPLabelName(label.c_str())
                .setColor(std::array<float, 4>{ color.r, color.g, color.b, color.a });
            _handle.insertDebugUtilsLabelEXT(info);
        }
        return *this;
    }

    gfx::CommandBuffer & CommandBuffer::BeginRendering(RenderParameters renderParameters) {
        gfx::CommandBuffer::BeginRendering();
        const auto framebuffer = _state.boundFramebuffer.value();
        return gfx::CommandBuffer::BeginRendering(framebuffer, renderParameters);
    }

    gfx::CommandBuffer& CommandBuffer::doBeginRendering(gfx::ResourceRef<const gfx::Framebuffer> framebuffer, RenderParameters renderParameters)
    {
        stateBeginRendering(framebuffer);
        std::vector<gfx::ImageBarrier> imageBarriers;
        for (const auto& attachment : framebuffer->getColorAttachments())
        {
            imageBarriers.push_back({
                attachment.get().getImage(),
                ResourceAccess::ColorAttachment
            });
        }
        if (framebuffer->hasDepthAttachment())
        {
            imageBarriers.push_back({
                framebuffer->getDepthAttachment().getImage(),
                ResourceAccess::DepthAttachment
            });
        }
        if (framebuffer->hasStencilAttachment())
        {
            imageBarriers.push_back({
                framebuffer->getStencilAttachment().getImage(),
                ResourceAccess::StencilAttachment
            });
        }
        Barrier({}, imageBarriers);

        std::vector<::vk::RenderingAttachmentInfoKHR> colorAttachmentInfos;
        int i = 0;
        for (auto& colorAttachment : framebuffer->getColorAttachments()) {
            auto attachInfo = ::vk::RenderingAttachmentInfoKHR()
                .setImageView(**dynamic_cast<const gfx::vk::ImageView*>(&colorAttachment.get()))
                .setImageLayout(::vk::ImageLayout::eColorAttachmentOptimal)
                .setClearValue(getVkClearValue(framebuffer->getClearColor(i)))
                .setLoadOp(getVkLoadOp(renderParameters.colorLoadOperation))
                .setStoreOp(getVkStoreOp(renderParameters.colorStoreOperation));
            if (framebuffer->hasResolveAttachments()) {
                attachInfo
                    .setResolveImageLayout(::vk::ImageLayout::eColorAttachmentOptimal)
                    .setResolveImageView(**dynamic_cast<const gfx::vk::ImageView*>(&framebuffer->getResolveAttachment(i)))
                    .setResolveMode(getVkResolveMode(framebuffer->getResolveMode()));
            }
            colorAttachmentInfos.push_back(attachInfo);
            i++;
        }

        const auto depthAttachment = framebuffer->hasDepthAttachment() ? std::optional(::vk::RenderingAttachmentInfoKHR()
            .setImageView(**dynamic_cast<const gfx::vk::ImageView*>(&framebuffer->getDepthAttachment()))
            .setImageLayout(::vk::ImageLayout::eDepthStencilAttachmentOptimal)
            .setClearValue(::vk::ClearValue().setDepthStencil({ framebuffer->getClearDepth(), static_cast<glm::u32>(framebuffer->getClearStencil()) }))
            .setLoadOp(getVkLoadOp(renderParameters.depthLoadOperation))
            .setStoreOp(getVkStoreOp(renderParameters.depthStoreOperation))) : std::nullopt;

        const auto stencilAttachment = framebuffer->hasStencilAttachment() ? std::optional(::vk::RenderingAttachmentInfoKHR()
            .setImageView(**dynamic_cast<const gfx::vk::ImageView*>(&framebuffer->getStencilAttachment()))
            .setImageLayout(::vk::ImageLayout::eDepthStencilAttachmentOptimal)
            .setClearValue(::vk::ClearValue().setDepthStencil({ framebuffer->getClearDepth(), static_cast<glm::u32>(framebuffer->getClearStencil()) }))
            .setLoadOp(getVkLoadOp(renderParameters.stencilLoadOperation))
            .setStoreOp(getVkStoreOp(renderParameters.stencilStoreOperation))) : std::nullopt;

        auto extent = framebuffer->getExtent();
        const auto renderArea = ::vk::Rect2D()
            .setOffset({0, 0})
            .setExtent({ extent.x, extent.y });

        const auto beginRenderingInfo = ::vk::RenderingInfoKHR()
            .setRenderArea(renderArea)
            .setLayerCount(1)
            .setViewMask(0)
            .setColorAttachments(colorAttachmentInfos)
            .setPDepthAttachment(depthAttachment.has_value() ? &depthAttachment.value() : nullptr)
            .setPStencilAttachment(stencilAttachment.has_value() ? &stencilAttachment.value() : nullptr);

        _handle.beginRenderingKHR(beginRenderingInfo);
        return *this;
    }

    gfx::CommandBuffer& CommandBuffer::EndRendering()
    {
        gfx::CommandBuffer::EndRendering();
        _handle.endRenderingKHR();
        return *this;
    }

    gfx::CommandBuffer& CommandBuffer::SetViewport(glm::u32 x, glm::u32 y, glm::u32 width, glm::u32 height)
    {
        gfx::CommandBuffer::SetViewport(x, y, width, height);
        // The app authors its geometry, texture sampling and gl_FragCoord conventions for
        // OpenGL's Y-up window origin; only the depth convention is shared (both use
        // GLM_FORCE_DEPTH_ZERO_TO_ONE). Present the default (swapchain) framebuffer through
        // a negative-height viewport so Vulkan's Y-down framebuffer matches OpenGL's Y-up
        // result on screen. Offscreen targets are sampled with the same convention on both
        // backends, so they use a straight viewport.
        const bool isDefaultFramebuffer = _state.boundFramebuffer.value()->IsDefault();
        const ::vk::Viewport viewport = ::vk::Viewport()
            .setX(static_cast<float>(x))
            .setY(static_cast<float>(y) + (isDefaultFramebuffer ? static_cast<float>(height) : 0.f))
            .setWidth(static_cast<float>(width))
            .setHeight(static_cast<float>(height) * (isDefaultFramebuffer ? -1.f : 1.f))
            .setMinDepth(0.f)
            .setMaxDepth(1.f);
        _handle.setViewport(0, viewport);
        return *this;
    }

    gfx::CommandBuffer& CommandBuffer::SetScissor(glm::u32 x, glm::u32 y, glm::u32 width, glm::u32 height)
    {
        gfx::CommandBuffer::SetScissor(x, y, width, height);
        const ::vk::Rect2D scissor = ::vk::Rect2D()
            .setOffset({ static_cast<glm::i32>(x), static_cast<glm::i32>(y) })
            .setExtent({ width, height });
        _handle.setScissor(0, scissor);
        return *this;
    }

    gfx::CommandBuffer& CommandBuffer::SetLineWidth(const float lineWidth)
    {
        gfx::CommandBuffer::SetLineWidth(lineWidth);
        _handle.setLineWidth(lineWidth);
        return *this;
    }

    gfx::CommandBuffer& CommandBuffer::SetDepthBias(const float constantFactor, const float clamp, const float slopeFactor)
    {
        gfx::CommandBuffer::SetDepthBias(constantFactor, clamp, slopeFactor);
        _handle.setDepthBias(constantFactor, clamp, slopeFactor);
        return *this;
    }

    gfx::CommandBuffer& CommandBuffer::SetBlendConstants(const glm::vec4 constants)
    {
        gfx::CommandBuffer::SetBlendConstants(constants);
        const float bc[4] = { constants.r, constants.g, constants.b, constants.a };
        _handle.setBlendConstants(bc);
        return *this;
    }

    gfx::CommandBuffer& CommandBuffer::SetStencilCompareMask(const StencilFace face, const glm::u32 compareMask)
    {
        gfx::CommandBuffer::SetStencilCompareMask(face, compareMask);
        _handle.setStencilCompareMask(getVkStencilFace(face), compareMask);
        return *this;
    }

    gfx::CommandBuffer& CommandBuffer::SetStencilWriteMask(const StencilFace face, const glm::u32 writeMask)
    {
        gfx::CommandBuffer::SetStencilWriteMask(face, writeMask);
        _handle.setStencilWriteMask(getVkStencilFace(face), writeMask);
        return *this;
    }

    gfx::CommandBuffer& CommandBuffer::SetStencilReference(const StencilFace face, const glm::u32 reference)
    {
        gfx::CommandBuffer::SetStencilReference(face, reference);
        _handle.setStencilReference(getVkStencilFace(face), reference);
        return *this;
    }

    gfx::CommandBuffer& CommandBuffer::SetCullMode(const Flags<CullMode> cullMode)
    {
        gfx::CommandBuffer::SetCullMode(cullMode);
        _handle.setCullMode(getVkCullMode(cullMode));
        return *this;
    }

    gfx::CommandBuffer& CommandBuffer::SetFrontFace(const FrontFace frontFace)
    {
        gfx::CommandBuffer::SetFrontFace(frontFace);
        // The front face is authored for OpenGL's Y-up window space. Offscreen targets
        // render with Vulkan's Y-down (straight) viewport, which reverses triangle winding
        // relative to that authoring, so invert the front face there. The default
        // framebuffer uses a negative-height (flipped) viewport (see SetViewport), which
        // restores the original winding, so it takes the winding as-is.
        const bool isDefaultFramebuffer = _state.boundFramebuffer.has_value() && _state.boundFramebuffer.value()->IsDefault();
        _handle.setFrontFace(isDefaultFramebuffer ? frontFace == gfx::FrontFace::eClockwise ? ::vk::FrontFace::eClockwise
                                                                                            : ::vk::FrontFace::eCounterClockwise
                                                  : frontFace == gfx::FrontFace::eClockwise ? ::vk::FrontFace::eCounterClockwise
                                                                                            : ::vk::FrontFace::eClockwise);
        return *this;
    }

    gfx::CommandBuffer& CommandBuffer::SetDepthTestEnable(const bool enable)
    {
        gfx::CommandBuffer::SetDepthTestEnable(enable);
        _handle.setDepthTestEnable(enable);
        return *this;
    }

    gfx::CommandBuffer& CommandBuffer::SetDepthWriteEnable(const bool enable)
    {
        gfx::CommandBuffer::SetDepthWriteEnable(enable);
        _handle.setDepthWriteEnable(enable);
        return *this;
    }

    gfx::CommandBuffer& CommandBuffer::SetDepthCompareOp(const CompareOp compareOp)
    {
        gfx::CommandBuffer::SetDepthCompareOp(compareOp);
        _handle.setDepthCompareOp(getVkCompareOp(compareOp));
        return *this;
    }

    gfx::CommandBuffer& CommandBuffer::SetStencilTestEnable(const bool enable)
    {
        gfx::CommandBuffer::SetStencilTestEnable(enable);
        _handle.setStencilTestEnable(enable);
        return *this;
    }

    gfx::CommandBuffer& CommandBuffer::SetStencilOp(const StencilFace face, const StencilOp failOp, const StencilOp passOp, const StencilOp depthFailOp, const CompareOp compareOp)
    {
        gfx::CommandBuffer::SetStencilOp(face, failOp, passOp, depthFailOp, compareOp);
        _handle.setStencilOp(getVkStencilFace(face), getVkStencilOp(failOp), getVkStencilOp(passOp), getVkStencilOp(depthFailOp), getVkCompareOp(compareOp));
        return *this;
    }

    gfx::CommandBuffer& CommandBuffer::SetDepthBiasEnable(const bool enable)
    {
        gfx::CommandBuffer::SetDepthBiasEnable(enable);
        _handle.setDepthBiasEnable(enable);
        return *this;
    }

    gfx::CommandBuffer& CommandBuffer::SetRasterizerDiscardEnable(const bool enable)
    {
        gfx::CommandBuffer::SetRasterizerDiscardEnable(enable);
        _handle.setRasterizerDiscardEnable(enable);
        return *this;
    }

    gfx::CommandBuffer& CommandBuffer::SetPrimitiveRestartEnable(const bool enable)
    {
        gfx::CommandBuffer::SetPrimitiveRestartEnable(enable);
        _handle.setPrimitiveRestartEnable(enable);
        return *this;
    }

    gfx::CommandBuffer& CommandBuffer::doBindComputePipeline(gfx::ResourceRef<const gfx::ComputePipeline> pipeline)
    {
        stateBindComputePipeline(pipeline);
        pipeline->Bind(*this);
        return *this;
    }

    gfx::CommandBuffer& CommandBuffer::doBindGraphicsPipeline(gfx::ResourceRef<const gfx::GraphicsPipeline> pipeline)
    {
        stateBindGraphicsPipeline(pipeline);
        pipeline->Bind(*this);
        // Dynamic state (front face included) is applied lazily before the first draw
        // via applyDynamicDefaults(), so nothing to emit here.
        return *this;
    }

    gfx::CommandBuffer& CommandBuffer::doBindRayTracingPipeline(gfx::ResourceRef<const gfx::RayTracingPipeline> pipeline)
    {
        stateBindRayTracingPipeline(pipeline);
        pipeline->Bind(*this);
        return *this;
    }

    gfx::CommandBuffer& CommandBuffer::TraceRays(const glm::u32 width, const glm::u32 height, const glm::u32 depth)
    {
        if (_failed) return *this;
        if (!_state.boundRayTracingPipeline.has_value())
            return record(ErrorCode::eNoRayTracingPipelineBound, "Cannot trace rays without a ray-tracing pipeline bound.");

        const auto& vkPipeline = dynamic_cast<const gfx::vk::RayTracingPipeline&>(*_state.boundRayTracingPipeline.value());
        _handle.traceRaysKHR(
            vkPipeline.getRaygenRegion(),
            vkPipeline.getMissRegion(),
            vkPipeline.getHitRegion(),
            vkPipeline.getCallableRegion(),
            width, height, depth);
        return *this;
    }

    gfx::CommandBuffer& CommandBuffer::doBindDescriptorSet(const glm::u32 index, gfx::ResourceRef<const gfx::DescriptorSet> set, const bool debug)
    {
        if (debug) set->DebugPrint();

        if (_state.boundComputePipeline.has_value()) {
            const auto& vkPipeline = dynamic_cast<const gfx::vk::ComputePipeline&>(*_state.boundComputePipeline.value());
            const auto pipelineLayout = vkPipeline.getPipelineLayout();
            const auto& vkSet = dynamic_cast<const gfx::vk::DescriptorSet&>(*set);
            _handle.bindDescriptorSets(
                ::vk::PipelineBindPoint::eCompute,
                pipelineLayout,
                index,
                *vkSet,
                nullptr);
            _state.boundComputeDescriptorSets.insert_or_assign(index, set);
        } else if (_state.boundGraphicsPipeline.has_value()) {
                const auto& vkPipeline = dynamic_cast<const gfx::vk::GraphicsPipeline&>(*_state.boundGraphicsPipeline.value());
                const auto pipelineLayout = vkPipeline.getPipelineLayout();
                const auto& vkSet = dynamic_cast<const gfx::vk::DescriptorSet&>(*set);
                _handle.bindDescriptorSets(
                    ::vk::PipelineBindPoint::eGraphics,
                    pipelineLayout,
                    index,
                    *vkSet,
                    nullptr);
                _state.boundGraphicsDescriptorSets.insert_or_assign(index, set);
        } else if (_state.boundRayTracingPipeline.has_value()) {
            const auto& vkPipeline = dynamic_cast<const gfx::vk::RayTracingPipeline&>(*_state.boundRayTracingPipeline.value());
            const auto pipelineLayout = vkPipeline.getPipelineLayout();
            const auto& vkSet = dynamic_cast<const gfx::vk::DescriptorSet&>(*set);
            _handle.bindDescriptorSets(
                ::vk::PipelineBindPoint::eRayTracingKHR,
                pipelineLayout,
                index,
                *vkSet,
                nullptr);
            _state.boundRayTracingDescriptorSets.insert_or_assign(index, set);
        } else {
            std::cerr << "No pipeline bound when trying to bind descriptor set" << std::endl;
        }
        return *this;
    }

    gfx::CommandBuffer& CommandBuffer::doBindMesh(gfx::ResourceRef<const gfx::Mesh> mesh)
    {
        stateBindMesh(mesh);
        std::vector<::vk::Buffer> vertexBuffers;
        std::vector<::vk::DeviceSize> offsets;
        for (const auto& buffer : mesh->getVertexBuffers()) {
            const auto& vkBuffer = dynamic_cast<const gfx::vk::Buffer&>(*buffer);
            vertexBuffers.push_back(*vkBuffer);
            offsets.push_back(0);
        }
        _handle.bindVertexBuffers(0, vertexBuffers, offsets);
        if (mesh->hasIndexBuffer())
        {
            const auto& vkBuffer = dynamic_cast<const gfx::vk::Buffer&>(*mesh->getIndexBuffer().value());
            _handle.bindIndexBuffer(*vkBuffer, 0, getVkIndexType(mesh->getIndexType().value()));
        }
        return *this;
    }

    gfx::CommandBuffer & CommandBuffer::doBarrier(
        const std::vector<gfx::BufferBarrier> bufferBarriers,
        const std::vector<gfx::ImageBarrier> imageBarriers) {

        ::vk::PipelineStageFlags srcStageMask = ::vk::PipelineStageFlagBits::eAllCommands;
        ::vk::PipelineStageFlags dstStageMask = ::vk::PipelineStageFlagBits::eNone;

        std::vector<::vk::BufferMemoryBarrier> vkBufferBarriers;
        std::vector<::vk::ImageMemoryBarrier> vkImageBarriers;
        for (const auto& barrier : bufferBarriers) {
            const auto& vkBuffer = dynamic_cast<const gfx::vk::Buffer&>(*barrier.getBuffer());
            vkBufferBarriers.push_back(::vk::BufferMemoryBarrier()
                .setSrcAccessMask(vkBuffer.getAccessMask())
                .setDstAccessMask(getVkAccessFlags(barrier.getDstAccess()))
                .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
                .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
                .setBuffer(*vkBuffer)
                .setOffset(barrier.getOffset())
                .setSize(barrier.getSize()));

            dstStageMask |= getVkPipelineStageFlags(barrier.getDstAccess());

            vkBuffer.setAccessMask(getVkAccessFlags(barrier.getDstAccess()));
        }
        for (const auto& barrier : imageBarriers) {
            const auto& vkImage = dynamic_cast<const gfx::vk::Image&>(*barrier.getImage());

            const auto newLayout = getVkImageLayout(barrier.getDstAccess());
            const auto dstAccessMask = getVkAccessFlags(barrier.getDstAccess());
            const auto aspectMask = getVkImageAspectFlags(vkImage.getFormat());

            const auto baseMip = barrier.getBaseMipLevel().value_or(0);
            const auto mipCount = barrier.getLevelCount().value_or(vkImage.getMipLevels());
            const auto baseLayer = barrier.getBaseArrayLayer().value_or(0);
            const auto layerCount = barrier.getLayerCount().value_or(vkImage.getArrayLayers());

            // The current (old) layout and access mask are tracked per subresource, and a
            // range can legitimately span subresources in different layouts — e.g. right
            // after GenerateMipmaps the last mip is still in TransferDst while the rest are
            // in TransferSrc. Emit one barrier per subresource with its own old layout so a
            // whole-image transition never stamps mip 0's layout onto every level.
            for (auto mip = baseMip; mip < baseMip + mipCount; ++mip) {
                for (auto layer = baseLayer; layer < baseLayer + layerCount; ++layer) {
                    vkImageBarriers.push_back(::vk::ImageMemoryBarrier()
                        .setSrcAccessMask(vkImage.getAccessMask(mip, layer))
                        .setDstAccessMask(dstAccessMask)
                        .setOldLayout(vkImage.getImageLayout(mip, layer))
                        .setNewLayout(newLayout)
                        .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
                        .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
                        .setImage(*vkImage)
                        .setSubresourceRange(::vk::ImageSubresourceRange()
                            .setAspectMask(aspectMask)
                            .setBaseMipLevel(mip)
                            .setLevelCount(1)
                            .setBaseArrayLayer(layer)
                            .setLayerCount(1)));

                    vkImage.SetImageLayout(newLayout, mip, layer);
                    vkImage.SetAccessMask(dstAccessMask, mip, layer);
                }
            }

            dstStageMask |= getVkPipelineStageFlags(barrier.getDstAccess());
        }

        _handle.pipelineBarrier(
            srcStageMask,
            dstStageMask,
            {},
            {},
            vkBufferBarriers,
            vkImageBarriers);

        return *this;
    }

    gfx::CommandBuffer& CommandBuffer::Dispatch(const glm::u32 groupCountX, const glm::u32 groupCountY, const glm::u32 groupCountZ)
    {
        if (_failed) return *this;
        _handle.dispatch(groupCountX, groupCountY, groupCountZ);
        return *this;
    }

    gfx::CommandBuffer & CommandBuffer::doDispatchIndirect(gfx::ResourceRef<const gfx::Buffer> indirectBuffer, glm::u64 offset) {
        if (_failed) return *this;
        const auto& vkBuffer = dynamic_cast<const gfx::vk::Buffer&>(*indirectBuffer);
        _handle.dispatchIndirect(*vkBuffer, offset);
        return *this;
    }

    gfx::CommandBuffer& CommandBuffer::DrawMeshTasks(const glm::u32 taskCountX, const glm::u32 taskCountY, const glm::u32 taskCountZ) {
        gfx::CommandBuffer::DrawMeshTasks(taskCountX, taskCountY, taskCountZ);
        if (_failed) return *this;
        applyDynamicDefaults();
        _handle.drawMeshTasksEXT(taskCountX, taskCountY, taskCountZ);
        return *this;
    }

    gfx::CommandBuffer & CommandBuffer::doDrawIndirect(gfx::ResourceRef<const gfx::Buffer> indirectBuffer, glm::u64 offset, glm::u32 drawCount, glm::u32 stride) {
        if (_failed) return *this;
        const auto& vkBuffer = dynamic_cast<const gfx::vk::Buffer&>(*indirectBuffer);
        applyDynamicDefaults();
        _handle.drawIndirect(*vkBuffer, offset, drawCount, stride);
        return *this;
    }

    gfx::CommandBuffer & CommandBuffer::doDrawIndexedIndirect(gfx::ResourceRef<const gfx::Buffer> indirectBuffer, glm::u64 offset, glm::u32 drawCount, glm::u32 stride) {
        if (_failed) return *this;
        const auto& vkBuffer = dynamic_cast<const gfx::vk::Buffer&>(*indirectBuffer);
        applyDynamicDefaults();
        _handle.drawIndexedIndirect(*vkBuffer, offset, drawCount, stride);
        return *this;
    }

    gfx::CommandBuffer & CommandBuffer::doDrawMeshTasksIndirect(gfx::ResourceRef<const gfx::Buffer> indirectBuffer, glm::u64 offset, glm::u32 drawCount, glm::u32 stride) {
        if (_failed) return *this;
        const auto& vkBuffer = dynamic_cast<const gfx::vk::Buffer&>(*indirectBuffer);
        applyDynamicDefaults();
        _handle.drawMeshTasksIndirectEXT(*vkBuffer, offset, drawCount, stride);
        return *this;
    }

    gfx::CommandBuffer& CommandBuffer::Draw(glm::u64 vertexCount, const glm::u32 instanceCount, const glm::u32 firstVertex, const glm::u32 firstInstance)
    {
        if (vertexCount == UINT64_MAX && _state.boundMesh.has_value()) {
            vertexCount = _state.boundMesh.value()->getVertexCount();
        }
        gfx::CommandBuffer::Draw(vertexCount, instanceCount, firstVertex, firstInstance);
        if (_failed) return *this;
        applyDynamicDefaults();
        _handle.draw(vertexCount, instanceCount, firstVertex, firstInstance);
        return *this;
    }

    gfx::CommandBuffer & CommandBuffer::DrawIndexed(glm::u64 indexCount, glm::u32 instanceCount, glm::u32 firstIndex, glm::i32 vertexOffset, glm::u32 firstInstance) {
        gfx::CommandBuffer::DrawIndexed(indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
        if (_failed) return *this;
        const auto mesh = _state.boundMesh.value();
        if (indexCount == UINT64_MAX) {
            indexCount = mesh->getIndexCount().value();
        }
        applyDynamicDefaults();
        _handle.drawIndexed(indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
        return *this;
    }

    gfx::CommandBuffer & CommandBuffer::doClearBuffer(gfx::ResourceRef<const gfx::Buffer> buffer, glm::u64 offset, glm::u64 size) {
        const auto& vkBuffer = dynamic_cast<const gfx::vk::Buffer&>(*buffer);
        _handle.fillBuffer(*vkBuffer, offset, size, 0);
        return *this;
    }

    gfx::CommandBuffer & CommandBuffer::doFillBuffer(gfx::ResourceRef<const gfx::Buffer> buffer, void *data, const glm::u64 offset, glm::u64 size) {
        const auto& vkBuffer = dynamic_cast<const gfx::vk::Buffer&>(*buffer);
        if (size == UINT64_MAX) {
            size = vkBuffer.getSize() - offset;
        }
        _handle.updateBuffer(*vkBuffer, offset, size, data);
        return *this;
    }

    gfx::CommandBuffer & CommandBuffer::doCopyBuffer(ResourceRef<const gfx::Buffer> srcBuffer, ResourceRef<const gfx::Buffer> dstBuffer, glm::u64 size, const glm::u64 srcOffset, const glm::u64 dstOffset) {
        if (_failed) return *this;
        const auto& vkSrcBuffer = dynamic_cast<const gfx::vk::Buffer&>(*srcBuffer);
        const auto& vkDstBuffer = dynamic_cast<const gfx::vk::Buffer&>(*dstBuffer);
        if (size == UINT64_MAX) {
            size = std::min(vkSrcBuffer.getSize() - srcOffset, vkDstBuffer.getSize() - dstOffset);
        }
        if (size > vkSrcBuffer.getSize() - srcOffset || size > vkDstBuffer.getSize() - dstOffset) {
            return record(ErrorCode::eCopySizeExceedsBuffer,
                std::format("Copy size {} exceeds buffer bounds (src size {}, dst size {}, srcOffset {}, dstOffset {}).",
                            size, vkSrcBuffer.getSize(), vkDstBuffer.getSize(), srcOffset, dstOffset));
        }

        _handle.copyBuffer(*vkSrcBuffer, *vkDstBuffer, ::vk::BufferCopy()
            .setSrcOffset(srcOffset)
            .setDstOffset(dstOffset)
            .setSize(size));
        return *this;
    }

    gfx::CommandBuffer& CommandBuffer::doClearColorImage(gfx::ResourceRef<const gfx::Image> image, const glm::vec4 color) {
        const auto& vkImage = dynamic_cast<const gfx::vk::Image&>(*image);

        // Transition every subresource to transfer-dst; this also updates the tracked
        // layout so the subsequent clear records against the correct layout.
        Barrier({}, {{ image, ResourceAccess::TransferDst }});

        ::vk::ClearValue clearValue;
        clearValue.color = ::vk::ClearColorValue(std::array<float, 4>{ color.r, color.g, color.b, color.a });
        vkImage.Clear(*this, clearValue);
        return *this;
    }

    gfx::CommandBuffer& CommandBuffer::doBlit(ResourceRef<const gfx::Image> srcImage, gfx::Blit blitInfo) {
        ResourceRef<const gfx::Image> dstImage =  dynamic_cast<const Scheduler&>(gfx::Context::Scheduler()).getSwapChain().getImage();
        Barrier({}, {
            {
                srcImage,
                ResourceAccess::TransferSrc,
                blitInfo.srcMipLevel,
                1,
                blitInfo.srcBaseArrayLayer,
                blitInfo.layerCount
            }, {
                dstImage,
                ResourceAccess::TransferDst,
                blitInfo.dstMipLevel,
                1,
                blitInfo.dstBaseArrayLayer,
                blitInfo.layerCount
            }
        });

        if (blitInfo.srcExtent == glm::ivec3(-1))
            blitInfo.srcExtent = srcImage->getExtent();
        if (blitInfo.dstExtent == glm::ivec3(-1))
            blitInfo.dstExtent = dstImage->getExtent();

        const auto& vkSrcImage = dynamic_cast<const Image&>(*srcImage);
        const auto& vkDstImage = dynamic_cast<const Image&>(*dstImage);

        _handle.blitImage(
            *vkSrcImage, ::vk::ImageLayout::eTransferSrcOptimal,
            *vkDstImage, ::vk::ImageLayout::eTransferDstOptimal,
            ::vk::ImageBlit()
                .setSrcSubresource(::vk::ImageSubresourceLayers()
                    .setAspectMask(vkSrcImage.getAspectFlags())
                    .setMipLevel(blitInfo.srcMipLevel)
                    .setBaseArrayLayer(blitInfo.srcBaseArrayLayer)
                    .setLayerCount(blitInfo.layerCount))
                .setSrcOffsets({
                    ::vk::Offset3D{ blitInfo.srcOffset.x, blitInfo.srcOffset.y, blitInfo.srcOffset.z },
                    ::vk::Offset3D{ blitInfo.srcOffset.x + blitInfo.srcExtent.x, blitInfo.srcOffset.y + blitInfo.srcExtent.y, blitInfo.srcOffset.z + blitInfo.srcExtent.z }
                })
                .setDstSubresource(::vk::ImageSubresourceLayers()
                    .setAspectMask(vkDstImage.getAspectFlags())
                    .setMipLevel(blitInfo.dstMipLevel)
                    .setBaseArrayLayer(blitInfo.dstBaseArrayLayer)
                    .setLayerCount(blitInfo.layerCount))
                .setDstOffsets({
                    ::vk::Offset3D{ blitInfo.dstOffset.x, blitInfo.dstOffset.y +  blitInfo.dstExtent.y, blitInfo.dstOffset.z },
                    ::vk::Offset3D{ blitInfo.dstOffset.x + blitInfo.dstExtent.x, blitInfo.dstOffset.y, blitInfo.dstOffset.z + blitInfo.dstExtent.z }
                }),
            ::vk::Filter::eNearest);

        return *this;
    }

    gfx::CommandBuffer& CommandBuffer::doBlit(gfx::ResourceRef<const gfx::Image> srcImage, gfx::ResourceRef<const gfx::Image> dstImage, gfx::Blit blitInfo)
    {
        if (blitInfo.srcExtent == glm::ivec3(-1))
            blitInfo.srcExtent = srcImage->getExtent();
        if (blitInfo.dstExtent == glm::ivec3(-1))
            blitInfo.dstExtent = dstImage->getExtent();

        Barrier({}, {
            {
                srcImage,
                ResourceAccess::TransferSrc,
                blitInfo.srcMipLevel,
                1,
                blitInfo.srcBaseArrayLayer,
                blitInfo.layerCount
            }, {
                dstImage,
                ResourceAccess::TransferDst,
                blitInfo.dstMipLevel,
                1,
                blitInfo.dstBaseArrayLayer,
                blitInfo.layerCount
            }
        });

        const auto& vkSrcImage = dynamic_cast<const Image&>(*srcImage);
        const auto& vkDstImage = dynamic_cast<const Image&>(*dstImage);

        _handle.blitImage(
            *vkSrcImage, ::vk::ImageLayout::eTransferSrcOptimal,
            *vkDstImage, ::vk::ImageLayout::eTransferDstOptimal,
            ::vk::ImageBlit()
                .setSrcSubresource(::vk::ImageSubresourceLayers()
                    .setAspectMask(vkSrcImage.getAspectFlags())
                    .setMipLevel(blitInfo.srcMipLevel)
                    .setBaseArrayLayer(blitInfo.srcBaseArrayLayer)
                    .setLayerCount(blitInfo.layerCount))
                .setSrcOffsets({
                    ::vk::Offset3D{ blitInfo.srcOffset.x, blitInfo.srcOffset.y, blitInfo.srcOffset.z },
                    ::vk::Offset3D{ blitInfo.srcOffset.x + blitInfo.srcExtent.x, blitInfo.srcOffset.y + blitInfo.srcExtent.y, blitInfo.srcOffset.z + blitInfo.srcExtent.z }
                })
                .setDstSubresource(::vk::ImageSubresourceLayers()
                    .setAspectMask(vkDstImage.getAspectFlags())
                    .setMipLevel(blitInfo.dstMipLevel)
                    .setBaseArrayLayer(blitInfo.dstBaseArrayLayer)
                    .setLayerCount(blitInfo.layerCount))
                .setDstOffsets({
                    ::vk::Offset3D{ blitInfo.dstOffset.x, blitInfo.dstOffset.y, blitInfo.dstOffset.z },
                    ::vk::Offset3D{ blitInfo.dstOffset.x + blitInfo.dstExtent.x, blitInfo.dstOffset.y + blitInfo.dstExtent.y, blitInfo.dstOffset.z + blitInfo.dstExtent.z }
                }),
            ::vk::Filter::eNearest);
         return *this;
    }

    gfx::CommandBuffer & CommandBuffer::doResolve(ResourceRef<const gfx::Image> srcImage, gfx::Resolve resolveInfo) {
        if (_failed) return *this;
        if (srcImage->getSampleCount() == SampleCount::e1)
            return record(ErrorCode::eResolveRequiresMultisample, "Resolve source image must be multisampled.");

        if (!_resolveHelperImage)
            _resolveHelperImage = gfx::Image::Builder()
                .setIsPerFrame(true)
                .setExtent(srcImage->getExtent())
                .setFormat(srcImage->getFormat())
                .setUsage(gfx::Image::Usage::eTransferDst)
                .addUsage(gfx::Image::Usage::eTransferSrc)
                .build();
        if (_resolveHelperImage->getExtent() != srcImage->getExtent())
            _resolveHelperImage->Resize(srcImage->getExtent());

        const auto& vkSrcImage = dynamic_cast<const Image&>(*srcImage);
        const auto& vkDstImage =  dynamic_cast<const Image&>(*_resolveHelperImage);

        if (resolveInfo.srcExtent == glm::ivec3(-1))
            resolveInfo.srcExtent = vkSrcImage.getExtent();
        if (resolveInfo.dstExtent == glm::ivec3(-1))
            resolveInfo.dstExtent = vkDstImage.getExtent();

        Barrier({}, {
            {
                srcImage,
                ResourceAccess::TransferSrc,
                resolveInfo.srcMipLevel,
                1,
                resolveInfo.srcBaseArrayLayer,
                resolveInfo.layerCount
            }, {
                _resolveHelperImage,
                ResourceAccess::TransferDst,
                resolveInfo.dstMipLevel,
                1,
                resolveInfo.dstBaseArrayLayer,
                resolveInfo.layerCount
            }
        });

        _handle.resolveImage(
            *vkSrcImage, ::vk::ImageLayout::eTransferSrcOptimal,
            *vkDstImage, ::vk::ImageLayout::eTransferDstOptimal,
            ::vk::ImageResolve()
                .setSrcSubresource(::vk::ImageSubresourceLayers()
                    .setAspectMask(vkSrcImage.getAspectFlags())
                    .setMipLevel(resolveInfo.srcMipLevel)
                    .setBaseArrayLayer(resolveInfo.srcBaseArrayLayer)
                    .setLayerCount(resolveInfo.layerCount))
                .setSrcOffset({ resolveInfo.srcOffset.x, resolveInfo.srcOffset.y, resolveInfo.srcOffset.z })
                .setDstSubresource(::vk::ImageSubresourceLayers()
                    .setAspectMask(vkDstImage.getAspectFlags())
                    .setMipLevel(resolveInfo.dstMipLevel)
                    .setBaseArrayLayer(resolveInfo.dstBaseArrayLayer)
                    .setLayerCount(resolveInfo.layerCount))
                .setDstOffset({ resolveInfo.dstOffset.x, resolveInfo.dstOffset.y, resolveInfo.dstOffset.z })
                .setExtent({ static_cast<glm::u32>(resolveInfo.srcExtent.x), static_cast<glm::u32>(resolveInfo.srcExtent.y), static_cast<glm::u32>(resolveInfo.srcExtent.z) }));

        Blit(gfx::ResourceRef<const gfx::Image>(_resolveHelperImage), gfx::Blit{});
        return *this;
    }

    gfx::CommandBuffer& CommandBuffer::doResolve(gfx::ResourceRef<const gfx::Image> srcImage, gfx::ResourceRef<const gfx::Image> dstImage, gfx::Resolve resolveInfo) {
        if (_failed) return *this;
        if (srcImage->getSampleCount() == SampleCount::e1)
            return record(ErrorCode::eResolveRequiresMultisample, "Resolve source image must be multisampled.");
        if (dstImage && dstImage->getSampleCount() != SampleCount::e1)
            return record(ErrorCode::eResolveRequiresMultisample, "Resolve destination image must not be multisampled.");

        const auto& vkSrcImage = dynamic_cast<const Image&>(*srcImage);
        const auto& vkDstImage =dynamic_cast<const Image&>(*dstImage);

        if (resolveInfo.srcExtent == glm::ivec3(-1))
            resolveInfo.srcExtent = vkSrcImage.getExtent();
        if (resolveInfo.dstExtent == glm::ivec3(-1))
            resolveInfo.dstExtent = vkDstImage.getExtent();

        Barrier({}, {
            {
                srcImage,
                ResourceAccess::TransferSrc,
                resolveInfo.srcMipLevel,
                1,
                resolveInfo.srcBaseArrayLayer,
                resolveInfo.layerCount
            }, {
                dstImage,
                ResourceAccess::TransferDst,
                resolveInfo.dstMipLevel,
                1,
                resolveInfo.dstBaseArrayLayer,
                resolveInfo.layerCount
            }
        });

        _handle.resolveImage(
            *vkSrcImage, ::vk::ImageLayout::eTransferSrcOptimal,
            *vkDstImage, ::vk::ImageLayout::eTransferDstOptimal,
            ::vk::ImageResolve()
                .setSrcSubresource(::vk::ImageSubresourceLayers()
                    .setAspectMask(vkSrcImage.getAspectFlags())
                    .setMipLevel(resolveInfo.srcMipLevel)
                    .setBaseArrayLayer(resolveInfo.srcBaseArrayLayer)
                    .setLayerCount(resolveInfo.layerCount))
                .setSrcOffset({ resolveInfo.srcOffset.x, resolveInfo.srcOffset.y, resolveInfo.srcOffset.z })
                .setDstSubresource(::vk::ImageSubresourceLayers()
                    .setAspectMask(vkDstImage.getAspectFlags())
                    .setMipLevel(resolveInfo.dstMipLevel)
                    .setBaseArrayLayer(resolveInfo.dstBaseArrayLayer)
                    .setLayerCount(resolveInfo.layerCount))
                .setDstOffset({ resolveInfo.dstOffset.x, resolveInfo.dstOffset.y, resolveInfo.dstOffset.z })
                .setExtent({ static_cast<glm::u32>(resolveInfo.srcExtent.x), static_cast<glm::u32>(resolveInfo.srcExtent.y), static_cast<glm::u32>(resolveInfo.srcExtent.z) }));

        return *this;
    }

    gfx::CommandBuffer& CommandBuffer::doCopyBufferToImage(ResourceRef<const gfx::Buffer> buffer, ResourceRef<const gfx::Image> image, gfx::Copy copyInfo) {
        if (_failed) return *this;
        const auto& vkBuffer = dynamic_cast<const gfx::vk::Buffer&>(*buffer);
        const auto& vkImage = dynamic_cast<const gfx::vk::Image&>(*image);

        if (copyInfo.bufferOffset >= vkBuffer.getSize())
            return record(ErrorCode::eCopySizeExceedsBuffer,
                std::format("Buffer offset {} exceeds buffer size {}.", copyInfo.bufferOffset, vkBuffer.getSize()));
        if (copyInfo.imageMipLevel >= vkImage.getMipLevels())
            return record(ErrorCode::eImageSubresourceOutOfRange,
                std::format("Image mip level {} exceeds image mip levels {}.", copyInfo.imageMipLevel, vkImage.getMipLevels()));
        if (copyInfo.imageBaseArrayLayer >= vkImage.getArrayLayers())
            return record(ErrorCode::eImageSubresourceOutOfRange,
                std::format("Image base array layer {} exceeds image array layers {}.", copyInfo.imageBaseArrayLayer, vkImage.getArrayLayers()));

        if (copyInfo.imageExtent == glm::ivec3(-1))
            copyInfo.imageExtent = image->getExtent();

        if (copyInfo.bufferRowLength == 0)
            copyInfo.bufferRowLength = copyInfo.imageExtent.x;
        if (copyInfo.bufferImageHeight == 0)
            copyInfo.bufferImageHeight = copyInfo.imageExtent.y;
        if (copyInfo.bufferRowLength < copyInfo.imageExtent.x || copyInfo.bufferImageHeight < copyInfo.imageExtent.y)
            return record(ErrorCode::eInvalidArgument,
                std::format("Buffer row length {} / image height {} too small for image extent {}x{}.",
                            copyInfo.bufferRowLength, copyInfo.bufferImageHeight, copyInfo.imageExtent.x, copyInfo.imageExtent.y));
        // Copy footprint in *bytes* (not texels): rowLength*imageHeight give the packed
        // texel counts, times depth and layer count, times the format's texel size. For
        // packed (unpadded) buffers this is the exact required size. Depth/stencil texel
        // sizes are conservatively over-estimated, which only makes the guard stricter.
        {
            const glm::u64 texelSize = static_cast<glm::u64>(Image::ChannelSizeFromImageFormat(image->getFormat()))
                                     * Image::ChannelCountFromImageFormat(image->getFormat());
            const glm::u64 copyBytes = static_cast<glm::u64>(copyInfo.bufferRowLength) * copyInfo.bufferImageHeight
                                     * copyInfo.imageExtent.z * copyInfo.imageLayerCount * texelSize;
            if (copyBytes + copyInfo.bufferOffset > vkBuffer.getSize())
                return record(ErrorCode::eCopySizeExceedsBuffer,
                    std::format("Buffer offset {} + copy size {} exceeds buffer size {}.",
                                copyInfo.bufferOffset, copyBytes, vkBuffer.getSize()));
        }
        if (copyInfo.imageBaseArrayLayer + copyInfo.imageLayerCount > vkImage.getArrayLayers())
            return record(ErrorCode::eImageSubresourceOutOfRange,
                std::format("Image base array layer {} + layer count {} exceeds image array layers {}.",
                            copyInfo.imageBaseArrayLayer, copyInfo.imageLayerCount, vkImage.getArrayLayers()));

        ImageBarrier({
            image,
            ResourceAccess::TransferDst,
            copyInfo.imageMipLevel,
            1,
            copyInfo.imageBaseArrayLayer,
            copyInfo.imageLayerCount
        });

        _handle.copyBufferToImage(
            *vkBuffer,
            *vkImage,
            ::vk::ImageLayout::eTransferDstOptimal,
            ::vk::BufferImageCopy()
                .setBufferOffset(copyInfo.bufferOffset)
                .setBufferRowLength(copyInfo.bufferRowLength)
                .setBufferImageHeight(copyInfo.bufferImageHeight)
                .setImageSubresource(::vk::ImageSubresourceLayers()
                    .setAspectMask(getVkImageAspectFlags(vkImage.getFormat()))
                    .setMipLevel(copyInfo.imageMipLevel)
                    .setBaseArrayLayer(copyInfo.imageBaseArrayLayer)
                    .setLayerCount(copyInfo.imageLayerCount))
                .setImageOffset({ copyInfo.imageOffset.x, copyInfo.imageOffset.y, copyInfo.imageOffset.z })
                .setImageExtent({ static_cast<uint32_t>(copyInfo.imageExtent.x), static_cast<uint32_t>(copyInfo.imageExtent.y), static_cast<uint32_t>(copyInfo.imageExtent.z) }));

        return *this;
    }

    gfx::CommandBuffer & CommandBuffer::doCopyImageToBuffer(ResourceRef<const gfx::Image> image, ResourceRef<const gfx::Buffer> buffer, gfx::Copy copyInfo) {
        if (_failed) return *this;
        const auto& vkBuffer = dynamic_cast<const gfx::vk::Buffer&>(*buffer);
        const auto& vkImage = dynamic_cast<const gfx::vk::Image&>(*image);

        if (copyInfo.bufferOffset >= vkBuffer.getSize())
            return record(ErrorCode::eCopySizeExceedsBuffer,
                std::format("Buffer offset {} exceeds buffer size {}.", copyInfo.bufferOffset, vkBuffer.getSize()));
        if (copyInfo.imageMipLevel >= vkImage.getMipLevels())
            return record(ErrorCode::eImageSubresourceOutOfRange,
                std::format("Image mip level {} exceeds image mip levels {}.", copyInfo.imageMipLevel, vkImage.getMipLevels()));
        if (copyInfo.imageBaseArrayLayer >= vkImage.getArrayLayers())
            return record(ErrorCode::eImageSubresourceOutOfRange,
                std::format("Image base array layer {} exceeds image array layers {}.", copyInfo.imageBaseArrayLayer, vkImage.getArrayLayers()));

        if (copyInfo.imageExtent == glm::ivec3(-1))
            copyInfo.imageExtent = image->getExtent();

        if (copyInfo.bufferRowLength == 0)
            copyInfo.bufferRowLength = copyInfo.imageExtent.x;
        if (copyInfo.bufferImageHeight == 0)
            copyInfo.bufferImageHeight = copyInfo.imageExtent.y;
        if (copyInfo.bufferRowLength < copyInfo.imageExtent.x || copyInfo.bufferImageHeight < copyInfo.imageExtent.y)
            return record(ErrorCode::eInvalidArgument,
                std::format("Buffer row length {} / image height {} too small for image extent {}x{}.",
                            copyInfo.bufferRowLength, copyInfo.bufferImageHeight, copyInfo.imageExtent.x, copyInfo.imageExtent.y));
        // Copy footprint in *bytes* (not texels): rowLength*imageHeight give the packed
        // texel counts, times depth and layer count, times the format's texel size. For
        // packed (unpadded) buffers this is the exact required size. Depth/stencil texel
        // sizes are conservatively over-estimated, which only makes the guard stricter.
        {
            const glm::u64 texelSize = static_cast<glm::u64>(Image::ChannelSizeFromImageFormat(image->getFormat()))
                                     * Image::ChannelCountFromImageFormat(image->getFormat());
            const glm::u64 copyBytes = static_cast<glm::u64>(copyInfo.bufferRowLength) * copyInfo.bufferImageHeight
                                     * copyInfo.imageExtent.z * copyInfo.imageLayerCount * texelSize;
            if (copyBytes + copyInfo.bufferOffset > vkBuffer.getSize())
                return record(ErrorCode::eCopySizeExceedsBuffer,
                    std::format("Buffer offset {} + copy size {} exceeds buffer size {}.",
                                copyInfo.bufferOffset, copyBytes, vkBuffer.getSize()));
        }
        if (copyInfo.imageBaseArrayLayer + copyInfo.imageLayerCount > vkImage.getArrayLayers())
            return record(ErrorCode::eImageSubresourceOutOfRange,
                std::format("Image base array layer {} + layer count {} exceeds image array layers {}.",
                            copyInfo.imageBaseArrayLayer, copyInfo.imageLayerCount, vkImage.getArrayLayers()));

        ImageBarrier({
            image,
            ResourceAccess::TransferSrc,
            copyInfo.imageMipLevel,
            1,
            copyInfo.imageBaseArrayLayer,
            copyInfo.imageLayerCount
        });

        _handle.copyImageToBuffer(
            *vkImage,
            ::vk::ImageLayout::eTransferSrcOptimal,
            *vkBuffer,
            ::vk::BufferImageCopy()
                .setBufferOffset(copyInfo.bufferOffset)
                .setBufferRowLength(copyInfo.bufferRowLength)
                .setBufferImageHeight(copyInfo.bufferImageHeight)
                .setImageSubresource(::vk::ImageSubresourceLayers()
                    .setAspectMask(getVkImageAspectFlags(vkImage.getFormat()))
                    .setMipLevel(copyInfo.imageMipLevel)
                    .setBaseArrayLayer(copyInfo.imageBaseArrayLayer)
                    .setLayerCount(copyInfo.imageLayerCount))
                .setImageOffset({ copyInfo.imageOffset.x, copyInfo.imageOffset.y, copyInfo.imageOffset.z })
                .setImageExtent({ static_cast<uint32_t>(copyInfo.imageExtent.x), static_cast<uint32_t>(copyInfo.imageExtent.y), static_cast<uint32_t>(copyInfo.imageExtent.z) }));

        return *this;
    }

    gfx::CommandBuffer& CommandBuffer::Run(const std::function<void(gfx::CommandBuffer&)>& command)
    {
        command(*this);
        return *this;
    }

    gfx::VoidResult CommandBuffer::Submit()
    {
        const auto commandBuffers = std::array { _handle };
        auto submitInfo = ::vk::SubmitInfo()
            .setCommandBuffers(commandBuffers);

        if (_signalSemaphore != nullptr)
            submitInfo.setSignalSemaphores(_signalSemaphore);

        // Submit regardless of recorded errors so the fence still signals (callers
        // WaitForFence afterwards); report the first error, if any, to the caller.
        try {
            _queue->submit(submitInfo, _fence);
        } catch (const std::exception& e) {
            record(ErrorCode::eBackend, e.what());
        }
        return result();
    }

    void CommandBuffer::Reset()
    {
        _state = {};
        _handle.reset();
    }

    void CommandBuffer::WaitForFence() const
    {
        try {
            auto result = Context::Device()->waitForFences(_fence, true, UINT64_MAX);
            if (result != ::vk::Result::eSuccess) {
                std::cerr << "Failed to wait for fence: " << ::vk::to_string(result) << std::endl;
            }

            Context::Device()->resetFences(_fence);
        } catch (const std::runtime_error& e) {
            std::cerr << e.what() << std::endl;
        }
    }

    gfx::CommandBuffer& CommandBuffer::PushConstants(const void *data, const glm::u32 size, const glm::u32 offset) {
        if (_state.boundComputePipeline.has_value()) {
            const auto& vkPipeline = dynamic_cast<const gfx::vk::ComputePipeline&>(*_state.boundComputePipeline.value());
            _handle.pushConstants(
                vkPipeline.getPipelineLayout(),
                ::vk::ShaderStageFlagBits::eCompute,
                offset,
                size,
                data);
        } else if (_state.boundGraphicsPipeline.has_value()) {
            const auto& vkPipeline = dynamic_cast<const gfx::vk::GraphicsPipeline&>(*_state.boundGraphicsPipeline.value());
            _handle.pushConstants(
                vkPipeline.getPipelineLayout(),
                getVkShaderStageFlags(vkPipeline.getPushConstantRange(offset).stages),
                offset,
                size,
                data);
        } else if (_state.boundRayTracingPipeline.has_value()) {
            const auto& vkPipeline = dynamic_cast<const gfx::vk::RayTracingPipeline&>(*_state.boundRayTracingPipeline.value());
            _handle.pushConstants(
                vkPipeline.getPipelineLayout(),
                getVkShaderStageFlags(vkPipeline.getPushConstantRange(offset).stages),
                offset,
                size,
                data);
        } else {
            std::cerr << "No pipeline bound when trying to push constants" << std::endl;
        }
        return *this;
    }
}
