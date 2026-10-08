//
// Created by radue on 2/27/2026.
//
#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#include "commandBuffer.h"
#include "surface.h"
#include "../../core/current.h"
#include <window.h>

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
#include "timeline.h"
#include "../../core/tokenState.h"
#include "vulkanContext.h"
#include "vk_enum_conversions.h"

#include <cstring>

namespace kor::vk
{
    class ComputePipeline;

    kor::Flags<CommandBuffer::Usage> getCommandBufferUsage(const kor::vk::Queue& queue)
    {
        kor::Flags<CommandBuffer::Usage> usage;
        if (queue.getFamily().getProperties().queueFlags & ::vk::QueueFlagBits::eGraphics)
            usage |= CommandBuffer::Usage::eGraphics;
        if (queue.getFamily().getProperties().queueFlags & ::vk::QueueFlagBits::eCompute)
            usage |= CommandBuffer::Usage::eCompute;
        if (queue.getFamily().getProperties().queueFlags & ::vk::QueueFlagBits::eTransfer)
            usage |= CommandBuffer::Usage::eTransfer;
        return usage;
    }

    CommandBuffer::CommandBuffer(const kor::vk::Queue& queue, const ::vk::CommandBuffer commandBuffer, const ::vk::CommandPool parentCommandPool,
                                 const ::vk::Fence fence, const ::vk::QueryPool timerPool)
        : kor::CommandBuffer(getCommandBufferUsage(queue)), _queue(queue), _parentPool(parentCommandPool) {
        _handle = commandBuffer;
        _fence = fence ? fence : kor::vk::Context::Device()->createFence({});

        // Timestamps are not universal: a queue family may report zero valid timestamp bits, which
        // is the driver saying this queue cannot be timed. Leaving the period at zero is what makes
        // SupportsTimers() false and turns the timer commands into no-ops on such a queue.
        if (queue.getFamily().getProperties().timestampValidBits > 0) {
            _timestampPeriod = Context::Runtime().getPhysicalDevice().getProperties().limits.timestampPeriod;
            if (_timestampPeriod > 0.f) {
                _timerPool = timerPool ? timerPool : Context::Device()->createQueryPool(::vk::QueryPoolCreateInfo()
                    .setQueryType(::vk::QueryType::eTimestamp)
                    .setQueryCount(MaxTimerScopes * 2));
            }
        }
    }

    CommandBuffer::~CommandBuffer() {
        // Back on the free list with its fence and query pool, for the next command buffer to reuse —
        // which submits with that fence, so it must be settled first, and outside the pool lock.
        DoWaitForFence();
        if (Context::Device().freeCommandBuffer(*this)) return;
        Context::Device()->destroyFence(_fence);
        if (_timerPool) Context::Device()->destroyQueryPool(_timerPool);
    }

    void CommandBuffer::Run(const std::function<void(const kor::vk::CommandBuffer&)>& command, ::vk::Semaphore waitSemaphore) const {
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

        try {
            const auto lock = Context::Device().lockQueues();
            _queue->submit(submitInfo, _fence);
            _fencePending = true;
        } catch (const std::runtime_error& e) {
            std::cerr << e.what() << std::endl;
        }
    }

    kor::CommandBuffer& CommandBuffer::DoBegin()
    {
        ResetErrors();
        ClearRecords();
        _inFlight.clear();
        // WaitForFence() resets the fence after waiting, but a caller who waited on a token instead
        // never went through it, and submitting with a still-signalled fence is invalid. Re-recording
        // means the last submission is done, though its fence may not have caught up yet.
        DoWaitForFence();
        // Before the pool is reset below, which is what destroys the results being collected.
        // Re-recording is proof the GPU is done with the last submission, so this is the earliest
        // moment the previous frame's timestamps can be read — and the reason they are read here.
        RetireTimers();
        constexpr auto commandBufferBeginInfo = ::vk::CommandBufferBeginInfo()
            .setFlags(::vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
        _handle.begin(commandBufferBeginInfo);
        return *this;
    }

    void CommandBuffer::DoEnd()
    {
        // Nothing recorded so far has reached the GPU. Work out where the barriers belong now
        // that the whole sequence is visible, then emit everything in order.
        ResolveBarriers();

        // A timestamp may only be written into a query that has been reset, and vkCmdResetQueryPool
        // is illegal inside a render pass. Here is the one point that satisfies both without
        // guessing: the recording is complete, so the exact number of scopes is known, and not a
        // single command has been emitted yet, so we are outside every pass the frame will open.
        // Sized to what was actually recorded, which is why a frame that opens no scope resets
        // nothing at all.
        if (const kor::u32 scopes = TimerScopeCount(); _timerPool && scopes > 0)
            _handle.resetQueryPool(_timerPool, 0, scopes * 2);

        SubmitTimers();
        EmitRecords();
        _handle.end();
    }

    kor::CommandBuffer& CommandBuffer::DoBeginDebugLabel(const std::string& label, const kor::Vec4 color)
    {
        return defer("BeginDebugLabel", [=, this] {
            // Guard on the loaded function pointer: VK_EXT_debug_utils is optional, so the
            // dispatcher entry is null when the instance was created without it.
            if (VULKAN_HPP_DEFAULT_DISPATCHER.vkCmdBeginDebugUtilsLabelEXT) {
                const auto info = ::vk::DebugUtilsLabelEXT()
                    .setPLabelName(label.c_str())
                    .setColor(std::array<float, 4>{ color.x, color.y, color.z, color.w });
                _handle.beginDebugUtilsLabelEXT(info);
            }
        });
    }

    kor::CommandBuffer& CommandBuffer::DoEndDebugLabel()
    {
        return defer("EndDebugLabel", [=, this] {
            if (VULKAN_HPP_DEFAULT_DISPATCHER.vkCmdEndDebugUtilsLabelEXT) {
                _handle.endDebugUtilsLabelEXT();
            }
        });
    }

    kor::CommandBuffer& CommandBuffer::DoInsertDebugLabel(const std::string& label, const kor::Vec4 color)
    {
        return defer("InsertDebugLabel", [=, this] {
            if (VULKAN_HPP_DEFAULT_DISPATCHER.vkCmdInsertDebugUtilsLabelEXT) {
                const auto info = ::vk::DebugUtilsLabelEXT()
                    .setPLabelName(label.c_str())
                    .setColor(std::array<float, 4>{ color.x, color.y, color.z, color.w });
                _handle.insertDebugUtilsLabelEXT(info);
            }
        });
    }

    kor::CommandBuffer& CommandBuffer::DoBeginRendering(const RenderInfo& renderInfo)
    {
        const auto framebuffer = renderInfo.Target();
        StateBeginRendering(framebuffer);
        // The attachment transitions are declared as uses by kor::CommandBuffer::BeginRendering
        // and emitted by the resolver *before* this record — they cannot be emitted here, since
        // by then the render pass is about to open and Vulkan forbids a transition inside one.

        std::vector<::vk::RenderingAttachmentInfoKHR> colorAttachmentInfos;
        kor::u32 i = 0;
        for (auto& colorAttachment : framebuffer->ColorAttachments()) {
            auto attachInfo = ::vk::RenderingAttachmentInfoKHR()
                .setImageView(**dynamic_cast<const kor::vk::ImageView*>(colorAttachment.view.Get()))
                .setImageLayout(::vk::ImageLayout::eColorAttachmentOptimal)
                .setClearValue(getVkClearValue(renderInfo.ClearColorAt(i)))
                .setLoadOp(getVkLoadOp(renderInfo.ColorLoadOperation()))
                .setStoreOp(getVkStoreOp(renderInfo.ColorStoreOperation()));
            if (framebuffer->ResolveAttachment(i).Valid()) {
                attachInfo
                    .setResolveImageLayout(::vk::ImageLayout::eColorAttachmentOptimal)
                    .setResolveImageView(**dynamic_cast<const kor::vk::ImageView*>(framebuffer->ResolveAttachment(i).Get()))
                    .setResolveMode(getVkResolveMode(framebuffer->ResolveMethod()));
            }
            colorAttachmentInfos.push_back(attachInfo);
            i++;
        }

        // One clear value for both slots: a combined depth/stencil format is one image, and the
        // pass carries one value for each half of it.
        const auto depthStencilClear = ::vk::ClearValue().setDepthStencil(
            { renderInfo.ClearDepth(), static_cast<kor::u32>(renderInfo.ClearStencil()) });

        const auto depthAttachment = framebuffer->HasDepthAttachment() ? std::optional(::vk::RenderingAttachmentInfoKHR()
            .setImageView(**dynamic_cast<const kor::vk::ImageView*>(framebuffer->DepthAttachment().Get()))
            .setImageLayout(::vk::ImageLayout::eDepthStencilAttachmentOptimal)
            .setClearValue(depthStencilClear)
            .setLoadOp(getVkLoadOp(renderInfo.DepthLoadOperation()))
            .setStoreOp(getVkStoreOp(renderInfo.DepthStoreOperation()))) : std::nullopt;

        const auto stencilAttachment = framebuffer->HasStencilAttachment() ? std::optional(::vk::RenderingAttachmentInfoKHR()
            .setImageView(**dynamic_cast<const kor::vk::ImageView*>(framebuffer->StencilAttachment().Get()))
            .setImageLayout(::vk::ImageLayout::eDepthStencilAttachmentOptimal)
            .setClearValue(depthStencilClear)
            .setLoadOp(getVkLoadOp(renderInfo.StencilLoadOperation()))
            .setStoreOp(getVkStoreOp(renderInfo.StencilStoreOperation()))) : std::nullopt;

        auto extent = framebuffer->Extent();
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

    kor::CommandBuffer& CommandBuffer::DoEndRendering()
    {
        return defer("EndRendering", [=, this] {
            _handle.endRenderingKHR();
        }, PassEdge::eCloses);
    }

    kor::CommandBuffer& CommandBuffer::DoSetViewport(kor::u32 x, kor::u32 y, kor::u32 width, kor::u32 height)
    {
        return defer("SetViewport", [=, this] {
            // Koral's canonical clip space is Vulkan's own, so the viewport is passed straight
            // through for every framebuffer — no negative height, no default/offscreen split.
            const ::vk::Viewport viewport = ::vk::Viewport()
                .setX(static_cast<float>(x))
                .setY(static_cast<float>(y))
                .setWidth(static_cast<float>(width))
                .setHeight(static_cast<float>(height))
                .setMinDepth(0.f)
                .setMaxDepth(1.f);
            _handle.setViewport(0, viewport);
        });
    }

    kor::CommandBuffer& CommandBuffer::DoSetScissor(kor::u32 x, kor::u32 y, kor::u32 width, kor::u32 height)
    {
        return defer("SetScissor", [=, this] {
            const ::vk::Rect2D scissor = ::vk::Rect2D()
                .setOffset({ static_cast<kor::i32>(x), static_cast<kor::i32>(y) })
                .setExtent({ width, height });
            _handle.setScissor(0, scissor);
        });
    }

    kor::CommandBuffer& CommandBuffer::DoSetLineWidth(const float lineWidth)
    {
        return defer("SetLineWidth", [=, this] {
            _handle.setLineWidth(lineWidth);
        });
    }

    kor::CommandBuffer& CommandBuffer::DoSetDepthBias(const float constantFactor, const float clamp, const float slopeFactor)
    {
        return defer("SetDepthBias", [=, this] {
            _handle.setDepthBias(constantFactor, clamp, slopeFactor);
        });
    }

    kor::CommandBuffer& CommandBuffer::DoSetBlendConstants(const kor::Vec4 constants)
    {
        return defer("SetBlendConstants", [=, this] {
            const float bc[4] = { constants.x, constants.y, constants.z, constants.w };
            _handle.setBlendConstants(bc);
        });
    }

    kor::CommandBuffer& CommandBuffer::DoSetStencilCompareMask(const StencilFace face, const kor::u32 compareMask)
    {
        return defer("SetStencilCompareMask", [=, this] {
            _handle.setStencilCompareMask(getVkStencilFace(face), compareMask);
        });
    }

    kor::CommandBuffer& CommandBuffer::DoSetStencilWriteMask(const StencilFace face, const kor::u32 writeMask)
    {
        return defer("SetStencilWriteMask", [=, this] {
            _handle.setStencilWriteMask(getVkStencilFace(face), writeMask);
        });
    }

    kor::CommandBuffer& CommandBuffer::DoSetStencilReference(const StencilFace face, const kor::u32 reference)
    {
        return defer("SetStencilReference", [=, this] {
            _handle.setStencilReference(getVkStencilFace(face), reference);
        });
    }

    kor::CommandBuffer& CommandBuffer::DoSetCullMode(const Flags<CullMode> cullMode)
    {
        return defer("SetCullMode", [=, this] {
            _handle.setCullMode(getVkCullMode(cullMode));
        });
    }

    kor::CommandBuffer& CommandBuffer::DoSetFrontFace(const FrontFace frontFace)
    {
        return defer("SetFrontFace", [=, this] {
            // Winding is canonical (Vulkan) too, so this is a plain pass-through.
            _handle.setFrontFace(getVkFrontFace(frontFace));
        });
    }

    kor::CommandBuffer& CommandBuffer::DoSetDepthTestEnable(const bool enable)
    {
        return defer("SetDepthTestEnable", [=, this] {
            _handle.setDepthTestEnable(enable);
        });
    }

    kor::CommandBuffer& CommandBuffer::DoSetDepthWriteEnable(const bool enable)
    {
        return defer("SetDepthWriteEnable", [=, this] {
            _handle.setDepthWriteEnable(enable);
        });
    }

    kor::CommandBuffer& CommandBuffer::DoSetDepthCompareOp(const CompareOp compareOp)
    {
        return defer("SetDepthCompareOp", [=, this] {
            _handle.setDepthCompareOp(getVkCompareOp(compareOp));
        });
    }

    kor::CommandBuffer& CommandBuffer::DoSetStencilTestEnable(const bool enable)
    {
        return defer("SetStencilTestEnable", [=, this] {
            _handle.setStencilTestEnable(enable);
        });
    }

    kor::CommandBuffer& CommandBuffer::DoSetStencilOp(const StencilFace face, const StencilOp failOp, const StencilOp passOp, const StencilOp depthFailOp, const CompareOp compareOp)
    {
        return defer("SetStencilOp", [=, this] {
            _handle.setStencilOp(getVkStencilFace(face), getVkStencilOp(failOp), getVkStencilOp(passOp), getVkStencilOp(depthFailOp), getVkCompareOp(compareOp));
        });
    }

    kor::CommandBuffer& CommandBuffer::DoSetDepthBiasEnable(const bool enable)
    {
        return defer("SetDepthBiasEnable", [=, this] {
            _handle.setDepthBiasEnable(enable);
        });
    }

    kor::CommandBuffer& CommandBuffer::DoSetRasterizerDiscardEnable(const bool enable)
    {
        return defer("SetRasterizerDiscardEnable", [=, this] {
            _handle.setRasterizerDiscardEnable(enable);
        });
    }

    kor::CommandBuffer& CommandBuffer::DoSetPrimitiveRestartEnable(const bool enable)
    {
        return defer("SetPrimitiveRestartEnable", [=, this] {
            _handle.setPrimitiveRestartEnable(enable);
        });
    }

    kor::CommandBuffer& CommandBuffer::DoBindComputePipeline(kor::ResourceRef<const kor::ComputePipeline> pipeline)
    {
        StateBindComputePipeline(pipeline);
        pipeline->Bind(*this);
        return *this;
    }

    kor::CommandBuffer& CommandBuffer::DoBindGraphicsPipeline(kor::ResourceRef<const kor::GraphicsPipeline> pipeline)
    {
        StateBindGraphicsPipeline(pipeline);
        pipeline->Bind(*this);
        // Dynamic state (front face included) is applied lazily before the first draw
        // via ApplyDynamicDefaults(), so nothing to emit here.
        return *this;
    }

    kor::CommandBuffer& CommandBuffer::DoBindRayTracingPipeline(kor::ResourceRef<const kor::RayTracingPipeline> pipeline)
    {
        StateBindRayTracingPipeline(pipeline);
        pipeline->Bind(*this);
        return *this;
    }

    kor::CommandBuffer& CommandBuffer::DoTraceRays(const kor::u32 width, const kor::u32 height, const kor::u32 depth, const std::source_location where)
    {
        return deferAt("TraceRays", where, [=, this] {
            const auto& vkPipeline = dynamic_cast<const kor::vk::RayTracingPipeline&>(*_state.boundRayTracingPipeline.value());
            _handle.traceRaysKHR(
                vkPipeline.getRaygenRegion(),
                vkPipeline.getMissRegion(),
                vkPipeline.getHitRegion(),
                vkPipeline.getCallableRegion(),
                width, height, depth);
        }, PassEdge::eNone, UsesForBoundResources(false), BoundPipelineUsesDeviceAddresses());
    }

    kor::CommandBuffer& CommandBuffer::DoBindDescriptorSet(const kor::u32 index, kor::ResourceRef<const kor::DescriptorSet> set)
    {

        if (_state.boundComputePipeline.has_value()) {
            const auto& vkPipeline = dynamic_cast<const kor::vk::ComputePipeline&>(*_state.boundComputePipeline.value());
            const auto pipelineLayout = vkPipeline.getPipelineLayout();
            const auto& vkSet = dynamic_cast<const kor::vk::DescriptorSet&>(*set);
            _handle.bindDescriptorSets(
                ::vk::PipelineBindPoint::eCompute,
                pipelineLayout,
                index,
                *vkSet,
                nullptr);
            _state.boundComputeDescriptorSets.insert_or_assign(index, set);
        } else if (_state.boundGraphicsPipeline.has_value()) {
                const auto& vkPipeline = dynamic_cast<const kor::vk::GraphicsPipeline&>(*_state.boundGraphicsPipeline.value());
                const auto pipelineLayout = vkPipeline.getPipelineLayout();
                const auto& vkSet = dynamic_cast<const kor::vk::DescriptorSet&>(*set);
                _handle.bindDescriptorSets(
                    ::vk::PipelineBindPoint::eGraphics,
                    pipelineLayout,
                    index,
                    *vkSet,
                    nullptr);
                _state.boundGraphicsDescriptorSets.insert_or_assign(index, set);
        } else if (_state.boundRayTracingPipeline.has_value()) {
            const auto& vkPipeline = dynamic_cast<const kor::vk::RayTracingPipeline&>(*_state.boundRayTracingPipeline.value());
            const auto pipelineLayout = vkPipeline.getPipelineLayout();
            const auto& vkSet = dynamic_cast<const kor::vk::DescriptorSet&>(*set);
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

    kor::CommandBuffer& CommandBuffer::DoBindMesh(kor::ResourceRef<const kor::Mesh> mesh)
    {
        StateBindMesh(mesh);
        std::vector<::vk::Buffer> vertexBuffers;
        std::vector<::vk::DeviceSize> offsets;
        for (const auto& buffer : mesh->VertexBuffers()) {
            const auto& vkBuffer = dynamic_cast<const kor::vk::Buffer&>(*buffer);
            vertexBuffers.push_back(*vkBuffer);
            offsets.push_back(0);
        }
        _handle.bindVertexBuffers(0, vertexBuffers, offsets);
        if (mesh->HasIndexBuffer())
        {
            const auto& vkBuffer = dynamic_cast<const kor::vk::Buffer&>(*mesh->IndexBuffer().value());
            _handle.bindIndexBuffer(*vkBuffer, 0, getVkIndexType(mesh->IndexType().value()));
        }
        return *this;
    }

    kor::CommandBuffer& CommandBuffer::DoBindVertexBuffer(const kor::u32 binding, kor::ResourceRef<const kor::Buffer> buffer, const kor::u64 offset)
    {
        StateBindVertexBuffer(binding, buffer);
        const auto& vkBuffer = dynamic_cast<const kor::vk::Buffer&>(*buffer);
        _handle.bindVertexBuffers(binding, *vkBuffer, offset);
        return *this;
    }

    kor::CommandBuffer & CommandBuffer::DoBarrier(
        const std::vector<kor::BufferBarrier> bufferBarriers,
        const std::vector<kor::ImageBarrier> imageBarriers) {

        ::vk::PipelineStageFlags srcStageMask = ::vk::PipelineStageFlagBits::eAllCommands;
        ::vk::PipelineStageFlags dstStageMask = ::vk::PipelineStageFlagBits::eNone;

        std::vector<::vk::BufferMemoryBarrier> vkBufferBarriers;
        std::vector<::vk::ImageMemoryBarrier> vkImageBarriers;
        for (const auto& barrier : bufferBarriers) {
            const auto& vkBuffer = dynamic_cast<const kor::vk::Buffer&>(*barrier.TargetBuffer());
            vkBufferBarriers.push_back(::vk::BufferMemoryBarrier()
                .setSrcAccessMask(vkBuffer.getAccessMask())
                .setDstAccessMask(getVkAccessFlags(barrier.DstAccess()))
                .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
                .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
                .setBuffer(*vkBuffer)
                .setOffset(barrier.Offset())
                .setSize(barrier.size()));

            dstStageMask |= getVkPipelineStageFlags(barrier.DstAccess());

            vkBuffer.setAccessMask(getVkAccessFlags(barrier.DstAccess()));
        }
        for (const auto& barrier : imageBarriers) {
            const auto& vkImage = dynamic_cast<const kor::vk::Image&>(*barrier.TargetImage());

            const auto newLayout = getVkImageLayout(barrier.DstAccess());
            const auto dstAccessMask = getVkAccessFlags(barrier.DstAccess());
            const auto aspectMask = getVkImageAspectFlags(vkImage.PixelFormat());

            // An absent count means "the rest of the image", so it is measured from the base
            // rather than from zero. Resolving it to the image's *total* count instead walked past
            // the last level whenever a base was given without one. Matches ResolveBarriers().
            const auto baseMip = barrier.BaseMipLevel().value_or(0);
            const auto mipCount = barrier.LevelCount().value_or(vkImage.MipLevels() - baseMip);
            const auto baseLayer = barrier.BaseArrayLayer().value_or(0);
            const auto layerCount = barrier.LayerCount().value_or(vkImage.ArrayLayers() - baseLayer);

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

            dstStageMask |= getVkPipelineStageFlags(barrier.DstAccess());
        }

        // A queue without graphics — the async compute queue, on a device whose second queue is of a
        // compute family — cannot name graphics stages, nor wait on accesses only they make. Those
        // came from the graphics queue, which a semaphore the submission waits on already made
        // available and visible; what is left of the barrier is its own stages and the transition.
        if (!(_queue.getFamily().getProperties().queueFlags & ::vk::QueueFlagBits::eGraphics)) {
            constexpr auto computeStages = ::vk::PipelineStageFlagBits::eTopOfPipe | ::vk::PipelineStageFlagBits::eDrawIndirect
                | ::vk::PipelineStageFlagBits::eComputeShader | ::vk::PipelineStageFlagBits::eTransfer
                | ::vk::PipelineStageFlagBits::eBottomOfPipe | ::vk::PipelineStageFlagBits::eHost;
            constexpr auto computeAccess = ::vk::AccessFlagBits::eIndirectCommandRead | ::vk::AccessFlagBits::eUniformRead
                | ::vk::AccessFlagBits::eShaderRead | ::vk::AccessFlagBits::eShaderWrite | ::vk::AccessFlagBits::eTransferRead
                | ::vk::AccessFlagBits::eTransferWrite | ::vk::AccessFlagBits::eHostRead | ::vk::AccessFlagBits::eHostWrite
                | ::vk::AccessFlagBits::eMemoryRead | ::vk::AccessFlagBits::eMemoryWrite;
            dstStageMask &= computeStages;
            if (!dstStageMask) dstStageMask = ::vk::PipelineStageFlagBits::eAllCommands;
            for (auto& barrier : vkBufferBarriers) {
                barrier.srcAccessMask &= computeAccess;
                barrier.dstAccessMask &= computeAccess;
            }
            for (auto& barrier : vkImageBarriers) {
                barrier.srcAccessMask &= computeAccess;
                barrier.dstAccessMask &= computeAccess;
            }
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

    kor::CommandBuffer& CommandBuffer::DoDispatch(const kor::u32 groupCountX, const kor::u32 groupCountY, const kor::u32 groupCountZ, const std::source_location where)
    {
        if (_failed) return *this;
        return deferAt("Dispatch", where, [=, this] {
            _handle.dispatch(groupCountX, groupCountY, groupCountZ);
        }, PassEdge::eNone, UsesForBoundResources(false), BoundPipelineUsesDeviceAddresses());
    }

    kor::CommandBuffer & CommandBuffer::DoDispatchIndirect(kor::ResourceRef<const kor::Buffer> indirectBuffer, kor::u64 offset) {
        if (_failed) return *this;
        const auto& vkBuffer = dynamic_cast<const kor::vk::Buffer&>(*indirectBuffer);
        _handle.dispatchIndirect(*vkBuffer, offset);
        return *this;
    }

    kor::CommandBuffer& CommandBuffer::DoDrawMeshTasks(const kor::u32 taskCountX, const kor::u32 taskCountY, const kor::u32 taskCountZ, const std::source_location where) {
        if (_failed) return *this;
        return deferAt("DrawMeshTasks", where, [=, this] {
            ApplyDynamicDefaults();
            _handle.drawMeshTasksEXT(taskCountX, taskCountY, taskCountZ);
        }, PassEdge::eNone, UsesForBoundResources(true), BoundPipelineUsesDeviceAddresses());
    }

    kor::CommandBuffer & CommandBuffer::DoDrawIndirect(kor::ResourceRef<const kor::Buffer> indirectBuffer, kor::u64 offset, kor::u32 drawCount, kor::u32 stride) {
        if (_failed) return *this;
        const auto& vkBuffer = dynamic_cast<const kor::vk::Buffer&>(*indirectBuffer);
        ApplyDynamicDefaults();
        _handle.drawIndirect(*vkBuffer, offset, drawCount, stride);
        return *this;
    }

    kor::CommandBuffer & CommandBuffer::DoDrawIndexedIndirect(kor::ResourceRef<const kor::Buffer> indirectBuffer, kor::u64 offset, kor::u32 drawCount, kor::u32 stride) {
        if (_failed) return *this;
        const auto& vkBuffer = dynamic_cast<const kor::vk::Buffer&>(*indirectBuffer);
        ApplyDynamicDefaults();
        _handle.drawIndexedIndirect(*vkBuffer, offset, drawCount, stride);
        return *this;
    }

    kor::CommandBuffer& CommandBuffer::DoDrawIndirectCount(kor::ResourceRef<const kor::Buffer> indirectBuffer, kor::u64 offset, kor::ResourceRef<const kor::Buffer> countBuffer,
                                                           kor::u64 countOffset, kor::u32 maxDrawCount, kor::u32 stride) {
        if (_failed) return *this;
        const auto& vkBuffer = dynamic_cast<const kor::vk::Buffer&>(*indirectBuffer);
        const auto& vkCount = dynamic_cast<const kor::vk::Buffer&>(*countBuffer);
        ApplyDynamicDefaults();
        _handle.drawIndirectCount(*vkBuffer, offset, *vkCount, countOffset, maxDrawCount, stride);
        return *this;
    }

    kor::CommandBuffer& CommandBuffer::DoDrawIndexedIndirectCount(kor::ResourceRef<const kor::Buffer> indirectBuffer, kor::u64 offset, kor::ResourceRef<const kor::Buffer> countBuffer,
                                                                  kor::u64 countOffset, kor::u32 maxDrawCount, kor::u32 stride) {
        if (_failed) return *this;
        const auto& vkBuffer = dynamic_cast<const kor::vk::Buffer&>(*indirectBuffer);
        const auto& vkCount = dynamic_cast<const kor::vk::Buffer&>(*countBuffer);
        ApplyDynamicDefaults();
        _handle.drawIndexedIndirectCount(*vkBuffer, offset, *vkCount, countOffset, maxDrawCount, stride);
        return *this;
    }

    kor::CommandBuffer & CommandBuffer::DoDrawMeshTasksIndirect(kor::ResourceRef<const kor::Buffer> indirectBuffer, kor::u64 offset, kor::u32 drawCount, kor::u32 stride) {
        if (_failed) return *this;
        const auto& vkBuffer = dynamic_cast<const kor::vk::Buffer&>(*indirectBuffer);
        ApplyDynamicDefaults();
        _handle.drawMeshTasksIndirectEXT(*vkBuffer, offset, drawCount, stride);
        return *this;
    }

    kor::CommandBuffer& CommandBuffer::DoDraw(const kor::u64 vertexCount, const kor::u32 instanceCount, const kor::u32 firstVertex, const kor::u32 firstInstance, const std::source_location where)
    {
        return deferAt("Draw", where, [=, this] {
            // At emit time the tracked state has replayed to this point, so the dynamic-state
            // mask is the one that was in force for *this* draw, not the end of recording.
            ApplyDynamicDefaults();
            _handle.draw(vertexCount, instanceCount, firstVertex, firstInstance);
        }, PassEdge::eNone, UsesForBoundResources(true), BoundPipelineUsesDeviceAddresses());
    }

    kor::CommandBuffer & CommandBuffer::DoDrawIndexed(const kor::u64 indexCount, const kor::u32 instanceCount, const kor::u32 firstIndex, const kor::i32 vertexOffset, const kor::u32 firstInstance, const std::source_location where) {
        return deferAt("DrawIndexed", where, [=, this] {
            ApplyDynamicDefaults();
            _handle.drawIndexed(indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
        }, PassEdge::eNone, UsesForBoundResources(true), BoundPipelineUsesDeviceAddresses());
    }

    kor::CommandBuffer & CommandBuffer::DoClearBuffer(kor::ResourceRef<const kor::Buffer> buffer, kor::u64 offset, kor::u64 size) {
        const auto& vkBuffer = dynamic_cast<const kor::vk::Buffer&>(*buffer);
        _handle.fillBuffer(*vkBuffer, offset, size, 0);
        return *this;
    }

    kor::CommandBuffer & CommandBuffer::DoFillBuffer(kor::ResourceRef<const kor::Buffer> buffer, const void* data, const kor::u64 offset, kor::u64 size) {
        const auto& vkBuffer = dynamic_cast<const kor::vk::Buffer&>(*buffer);
        if (size == WholeSize) {
            size = vkBuffer.size() - offset;
        }
        _handle.updateBuffer(*vkBuffer, offset, size, data);
        return *this;
    }

    kor::CommandBuffer & CommandBuffer::DoCopyBuffer(ResourceRef<const kor::Buffer> srcBuffer, ResourceRef<const kor::Buffer> dstBuffer, kor::u64 size, const kor::u64 srcOffset, const kor::u64 dstOffset) {
        if (_failed) return *this;
        const auto& vkSrcBuffer = dynamic_cast<const kor::vk::Buffer&>(*srcBuffer);
        const auto& vkDstBuffer = dynamic_cast<const kor::vk::Buffer&>(*dstBuffer);
        if (size == WholeSize) {
            size = std::min(vkSrcBuffer.size() - srcOffset, vkDstBuffer.size() - dstOffset);
        }
        if (size > vkSrcBuffer.size() - srcOffset || size > vkDstBuffer.size() - dstOffset) {
            return RecordError(ErrorCode::eCopySizeExceedsBuffer,
                std::format("Copy size {} exceeds buffer bounds (src size {}, dst size {}, srcOffset {}, dstOffset {}).",
                            size, vkSrcBuffer.size(), vkDstBuffer.size(), srcOffset, dstOffset));
        }

        _handle.copyBuffer(*vkSrcBuffer, *vkDstBuffer, ::vk::BufferCopy()
            .setSrcOffset(srcOffset)
            .setDstOffset(dstOffset)
            .setSize(size));
        return *this;
    }

    kor::CommandBuffer& CommandBuffer::DoClearColorImage(kor::ResourceRef<const kor::Image> image, const kor::Vec4 color) {
        const auto& vkImage = dynamic_cast<const kor::vk::Image&>(*image);

        ::vk::ClearValue clearValue;
        clearValue.color = ::vk::ClearColorValue(std::array<float, 4>{ color.x, color.y, color.z, color.w });
        vkImage.Clear(*this, clearValue);
        return *this;
    }

    kor::CommandBuffer& CommandBuffer::DoBlitToScreen(ResourceRef<const kor::Image> srcImage, kor::Blit blitInfo) {
        ResourceRef<const kor::Image> dstImage = dynamic_cast<const kor::vk::Surface&>(
            kor::detail::CurrentWindow("BlitToScreen").RenderSurface()).swapChain().image();
        Barrier({}, {
            {
                srcImage,
                ResourceAccess::eTransferSrc,
                blitInfo.srcMipLevel,
                1,
                blitInfo.srcBaseArrayLayer,
                blitInfo.layerCount
            }, {
                dstImage,
                ResourceAccess::eTransferDst,
                blitInfo.dstMipLevel,
                1,
                blitInfo.dstBaseArrayLayer,
                blitInfo.layerCount
            }
        });

        if (blitInfo.srcExtent == kor::IVec3(-1))
            blitInfo.srcExtent = kor::IVec3(srcImage->Extent());
        if (blitInfo.dstExtent == kor::IVec3(-1))
            blitInfo.dstExtent = kor::IVec3(dstImage->Extent());

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
                // Straight, like every other blit. This one used to invert Y (dstOffset.y + extent
                // as the first corner) because offscreen targets were rendered for OpenGL's Y-up
                // window origin, which left them stored upside down with respect to the swapchain.
                // Rendering is canonical now — scene-top is row 0 in both the source image and the
                // swapchain — so the inversion would flip a correct image.
                .setDstOffsets({
                    ::vk::Offset3D{ blitInfo.dstOffset.x, blitInfo.dstOffset.y, blitInfo.dstOffset.z },
                    ::vk::Offset3D{ blitInfo.dstOffset.x + blitInfo.dstExtent.x, blitInfo.dstOffset.y + blitInfo.dstExtent.y, blitInfo.dstOffset.z + blitInfo.dstExtent.z }
                }),
            getVkFilter(blitInfo.filtering));

        return *this;
    }

    kor::CommandBuffer& CommandBuffer::DoBlit(kor::ResourceRef<const kor::Image> srcImage, kor::ResourceRef<const kor::Image> dstImage, kor::Blit blitInfo)
    {
        if (blitInfo.srcExtent == kor::IVec3(-1))
            blitInfo.srcExtent = kor::IVec3(srcImage->Extent());
        if (blitInfo.dstExtent == kor::IVec3(-1))
            blitInfo.dstExtent = kor::IVec3(dstImage->Extent());

        // Both operands are declared as uses by the core wrapper; the resolver
        // emits their transitions ahead of this record.

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
            getVkFilter(blitInfo.filtering));
         return *this;
    }

    kor::CommandBuffer& CommandBuffer::DoCopyImage(kor::ResourceRef<const kor::Image> srcImage, kor::ResourceRef<const kor::Image> dstImage)
    {
        // Both operands are declared as uses by the core wrapper, which also checked they match.
        const auto& vkSrcImage = dynamic_cast<const Image&>(*srcImage);
        const auto& vkDstImage = dynamic_cast<const Image&>(*dstImage);
        const kor::UVec3 extent = srcImage->Extent();

        std::vector<::vk::ImageCopy> regions;
        for (kor::u32 mip = 0; mip < srcImage->MipLevels(); ++mip) {
            const auto layers = ::vk::ImageSubresourceLayers()
                .setAspectMask(vkSrcImage.getAspectFlags())
                .setMipLevel(mip)
                .setBaseArrayLayer(0)
                .setLayerCount(srcImage->ArrayLayers());
            regions.push_back(::vk::ImageCopy()
                .setSrcSubresource(layers)
                .setDstSubresource(layers)
                .setExtent({ std::max(1u, extent.x >> mip), std::max(1u, extent.y >> mip), std::max(1u, extent.z >> mip) }));
        }
        _handle.copyImage(*vkSrcImage, ::vk::ImageLayout::eTransferSrcOptimal,
                          *vkDstImage, ::vk::ImageLayout::eTransferDstOptimal, regions);
        return *this;
    }

    kor::CommandBuffer & CommandBuffer::DoResolveToScreen(ResourceRef<const kor::Image> srcImage, kor::Resolve resolveInfo) {
        if (_failed) return *this;
        if (srcImage->Samples() == SampleCount::e1)
            return RecordError(ErrorCode::eResolveRequiresMultisample, "Resolve source image must be multisampled.");

        if (!_resolveHelperImage)
            _resolveHelperImage = kor::Image::Builder()
                .SetIsPerFrame(true)
                .SetExtent(srcImage->Extent())
                .SetFormat(srcImage->PixelFormat())
                // Resolved into, then copied out of.
                .SetUsage(kor::Image::Usage::eTransferDst | kor::Image::Usage::eTransferSrc)
                .Build();
        if (_resolveHelperImage->Extent() != srcImage->Extent())
            _resolveHelperImage->Resize(srcImage->Extent());

        const auto& vkSrcImage = dynamic_cast<const Image&>(*srcImage);
        const auto& vkDstImage =  dynamic_cast<const Image&>(*_resolveHelperImage);

        if (resolveInfo.srcExtent == kor::IVec3(-1))
            resolveInfo.srcExtent = kor::IVec3(vkSrcImage.Extent());
        if (resolveInfo.dstExtent == kor::IVec3(-1))
            resolveInfo.dstExtent = kor::IVec3(vkDstImage.Extent());

        Barrier({}, {
            {
                srcImage,
                ResourceAccess::eTransferSrc,
                resolveInfo.srcMipLevel,
                1,
                resolveInfo.srcBaseArrayLayer,
                resolveInfo.layerCount
            }, {
                _resolveHelperImage,
                ResourceAccess::eTransferDst,
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
                .setExtent({ static_cast<kor::u32>(resolveInfo.srcExtent.x), static_cast<kor::u32>(resolveInfo.srcExtent.y), static_cast<kor::u32>(resolveInfo.srcExtent.z) }));

        // Resolved into the helper above; this is the step that puts it on screen.
        BlitToScreen(_resolveHelperImage, kor::Blit{});
        return *this;
    }

    kor::CommandBuffer& CommandBuffer::DoResolve(kor::ResourceRef<const kor::Image> srcImage, kor::ResourceRef<const kor::Image> dstImage, kor::Resolve resolveInfo) {
        if (_failed) return *this;
        if (srcImage->Samples() == SampleCount::e1)
            return RecordError(ErrorCode::eResolveRequiresMultisample, "Resolve source image must be multisampled.");
        if (dstImage && dstImage->Samples() != SampleCount::e1)
            return RecordError(ErrorCode::eResolveRequiresMultisample, "Resolve destination image must not be multisampled.");

        const auto& vkSrcImage = dynamic_cast<const Image&>(*srcImage);
        const auto& vkDstImage =dynamic_cast<const Image&>(*dstImage);

        if (resolveInfo.srcExtent == kor::IVec3(-1))
            resolveInfo.srcExtent = kor::IVec3(vkSrcImage.Extent());
        if (resolveInfo.dstExtent == kor::IVec3(-1))
            resolveInfo.dstExtent = kor::IVec3(vkDstImage.Extent());

        Barrier({}, {
            {
                srcImage,
                ResourceAccess::eTransferSrc,
                resolveInfo.srcMipLevel,
                1,
                resolveInfo.srcBaseArrayLayer,
                resolveInfo.layerCount
            }, {
                dstImage,
                ResourceAccess::eTransferDst,
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
                .setExtent({ static_cast<kor::u32>(resolveInfo.srcExtent.x), static_cast<kor::u32>(resolveInfo.srcExtent.y), static_cast<kor::u32>(resolveInfo.srcExtent.z) }));

        return *this;
    }

    kor::CommandBuffer& CommandBuffer::DoCopyBufferToImage(ResourceRef<const kor::Buffer> buffer, ResourceRef<const kor::Image> image, kor::Copy copyInfo) {
        if (_failed) return *this;
        const auto& vkBuffer = dynamic_cast<const kor::vk::Buffer&>(*buffer);
        const auto& vkImage = dynamic_cast<const kor::vk::Image&>(*image);

        if (copyInfo.bufferOffset >= vkBuffer.size())
            return RecordError(ErrorCode::eCopySizeExceedsBuffer,
                std::format("Buffer offset {} exceeds buffer size {}.", copyInfo.bufferOffset, vkBuffer.size()));
        if (copyInfo.imageMipLevel >= vkImage.MipLevels())
            return RecordError(ErrorCode::eImageSubresourceOutOfRange,
                std::format("Image mip level {} exceeds image mip levels {}.", copyInfo.imageMipLevel, vkImage.MipLevels()));
        if (copyInfo.imageBaseArrayLayer >= vkImage.ArrayLayers())
            return RecordError(ErrorCode::eImageSubresourceOutOfRange,
                std::format("Image base array layer {} exceeds image array layers {}.", copyInfo.imageBaseArrayLayer, vkImage.ArrayLayers()));

        if (copyInfo.imageExtent == kor::IVec3(-1))
            copyInfo.imageExtent = kor::IVec3(image->Extent());

        if (copyInfo.bufferRowLength == 0)
            copyInfo.bufferRowLength = copyInfo.imageExtent.x;
        if (copyInfo.bufferImageHeight == 0)
            copyInfo.bufferImageHeight = copyInfo.imageExtent.y;

        // A compressed format is addressed in blocks, and Vulkan requires the buffer's row length and
        // image height to be whole numbers of them. The last mip levels of any texture are smaller
        // than one block — a 2x2 level of a 4x4 format — so rounding up here is not an edge case but
        // the ordinary end of every mip chain.
        if (Image::IsBlockCompressed(image->PixelFormat())) {
            const auto block = Image::BlockExtent(image->PixelFormat());
            const auto roundUp = [](const kor::u32 value, const kor::u32 to) { return (value + to - 1) / to * to; };
            copyInfo.bufferRowLength = roundUp(copyInfo.bufferRowLength, block.x);
            copyInfo.bufferImageHeight = roundUp(copyInfo.bufferImageHeight, block.y);
        }
        if (copyInfo.bufferRowLength < copyInfo.imageExtent.x || copyInfo.bufferImageHeight < copyInfo.imageExtent.y)
            return RecordError(ErrorCode::eInvalidArgument,
                std::format("Buffer row length {} / image height {} too small for image extent {}x{}.",
                            copyInfo.bufferRowLength, copyInfo.bufferImageHeight, copyInfo.imageExtent.x, copyInfo.imageExtent.y));
        // Copy footprint in *bytes* (not texels): rowLength/imageHeight give the packed extent, and
        // sizeOfRegion turns it into bytes — counting blocks for a compressed format and texels for
        // an uncompressed one, so a BC7 upload is measured in the units it is actually stored in
        // rather than in texels it does not have. For packed (unpadded) buffers this is the exact
        // required size. Depth/stencil texel sizes are conservatively over-estimated, which only
        // makes the guard stricter.
        {
            const kor::u64 copyBytes = Image::SizeOfRegion(
                image->PixelFormat(),
                { kor::u32(copyInfo.bufferRowLength), kor::u32(copyInfo.bufferImageHeight), kor::u32(copyInfo.imageExtent.z) },
                copyInfo.imageLayerCount);
            if (copyBytes + copyInfo.bufferOffset > vkBuffer.size())
                return RecordError(ErrorCode::eCopySizeExceedsBuffer,
                    std::format("Buffer offset {} + copy size {} exceeds buffer size {}.",
                                copyInfo.bufferOffset, copyBytes, vkBuffer.size()));
        }
        if (copyInfo.imageBaseArrayLayer + copyInfo.imageLayerCount > vkImage.ArrayLayers())
            return RecordError(ErrorCode::eImageSubresourceOutOfRange,
                std::format("Image base array layer {} + layer count {} exceeds image array layers {}.",
                            copyInfo.imageBaseArrayLayer, copyInfo.imageLayerCount, vkImage.ArrayLayers()));

        ImageBarrier({
            image,
            ResourceAccess::eTransferDst,
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
                    .setAspectMask(getVkImageAspectFlags(vkImage.PixelFormat()))
                    .setMipLevel(copyInfo.imageMipLevel)
                    .setBaseArrayLayer(copyInfo.imageBaseArrayLayer)
                    .setLayerCount(copyInfo.imageLayerCount))
                .setImageOffset({ copyInfo.imageOffset.x, copyInfo.imageOffset.y, copyInfo.imageOffset.z })
                .setImageExtent({ static_cast<uint32_t>(copyInfo.imageExtent.x), static_cast<uint32_t>(copyInfo.imageExtent.y), static_cast<uint32_t>(copyInfo.imageExtent.z) }));

        return *this;
    }

    kor::CommandBuffer & CommandBuffer::DoCopyImageToBuffer(ResourceRef<const kor::Image> image, ResourceRef<const kor::Buffer> buffer, kor::Copy copyInfo) {
        if (_failed) return *this;
        const auto& vkBuffer = dynamic_cast<const kor::vk::Buffer&>(*buffer);
        const auto& vkImage = dynamic_cast<const kor::vk::Image&>(*image);

        if (copyInfo.bufferOffset >= vkBuffer.size())
            return RecordError(ErrorCode::eCopySizeExceedsBuffer,
                std::format("Buffer offset {} exceeds buffer size {}.", copyInfo.bufferOffset, vkBuffer.size()));
        if (copyInfo.imageMipLevel >= vkImage.MipLevels())
            return RecordError(ErrorCode::eImageSubresourceOutOfRange,
                std::format("Image mip level {} exceeds image mip levels {}.", copyInfo.imageMipLevel, vkImage.MipLevels()));
        if (copyInfo.imageBaseArrayLayer >= vkImage.ArrayLayers())
            return RecordError(ErrorCode::eImageSubresourceOutOfRange,
                std::format("Image base array layer {} exceeds image array layers {}.", copyInfo.imageBaseArrayLayer, vkImage.ArrayLayers()));

        if (copyInfo.imageExtent == kor::IVec3(-1))
            copyInfo.imageExtent = kor::IVec3(image->Extent());

        if (copyInfo.bufferRowLength == 0)
            copyInfo.bufferRowLength = copyInfo.imageExtent.x;
        if (copyInfo.bufferImageHeight == 0)
            copyInfo.bufferImageHeight = copyInfo.imageExtent.y;

        // A compressed format is addressed in blocks, and Vulkan requires the buffer's row length and
        // image height to be whole numbers of them. The last mip levels of any texture are smaller
        // than one block — a 2x2 level of a 4x4 format — so rounding up here is not an edge case but
        // the ordinary end of every mip chain.
        if (Image::IsBlockCompressed(image->PixelFormat())) {
            const auto block = Image::BlockExtent(image->PixelFormat());
            const auto roundUp = [](const kor::u32 value, const kor::u32 to) { return (value + to - 1) / to * to; };
            copyInfo.bufferRowLength = roundUp(copyInfo.bufferRowLength, block.x);
            copyInfo.bufferImageHeight = roundUp(copyInfo.bufferImageHeight, block.y);
        }
        if (copyInfo.bufferRowLength < copyInfo.imageExtent.x || copyInfo.bufferImageHeight < copyInfo.imageExtent.y)
            return RecordError(ErrorCode::eInvalidArgument,
                std::format("Buffer row length {} / image height {} too small for image extent {}x{}.",
                            copyInfo.bufferRowLength, copyInfo.bufferImageHeight, copyInfo.imageExtent.x, copyInfo.imageExtent.y));
        // Copy footprint in *bytes* (not texels): rowLength/imageHeight give the packed extent, and
        // sizeOfRegion turns it into bytes — counting blocks for a compressed format and texels for
        // an uncompressed one, so a BC7 upload is measured in the units it is actually stored in
        // rather than in texels it does not have. For packed (unpadded) buffers this is the exact
        // required size. Depth/stencil texel sizes are conservatively over-estimated, which only
        // makes the guard stricter.
        {
            const kor::u64 copyBytes = Image::SizeOfRegion(
                image->PixelFormat(),
                { kor::u32(copyInfo.bufferRowLength), kor::u32(copyInfo.bufferImageHeight), kor::u32(copyInfo.imageExtent.z) },
                copyInfo.imageLayerCount);
            if (copyBytes + copyInfo.bufferOffset > vkBuffer.size())
                return RecordError(ErrorCode::eCopySizeExceedsBuffer,
                    std::format("Buffer offset {} + copy size {} exceeds buffer size {}.",
                                copyInfo.bufferOffset, copyBytes, vkBuffer.size()));
        }
        if (copyInfo.imageBaseArrayLayer + copyInfo.imageLayerCount > vkImage.ArrayLayers())
            return RecordError(ErrorCode::eImageSubresourceOutOfRange,
                std::format("Image base array layer {} + layer count {} exceeds image array layers {}.",
                            copyInfo.imageBaseArrayLayer, copyInfo.imageLayerCount, vkImage.ArrayLayers()));

        ImageBarrier({
            image,
            ResourceAccess::eTransferSrc,
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
                    .setAspectMask(getVkImageAspectFlags(vkImage.PixelFormat()))
                    .setMipLevel(copyInfo.imageMipLevel)
                    .setBaseArrayLayer(copyInfo.imageBaseArrayLayer)
                    .setLayerCount(copyInfo.imageLayerCount))
                .setImageOffset({ copyInfo.imageOffset.x, copyInfo.imageOffset.y, copyInfo.imageOffset.z })
                .setImageExtent({ static_cast<uint32_t>(copyInfo.imageExtent.x), static_cast<uint32_t>(copyInfo.imageExtent.y), static_cast<uint32_t>(copyInfo.imageExtent.z) }));

        return *this;
    }

    kor::CommandBuffer& CommandBuffer::DoRun(const std::function<void(kor::CommandBuffer&)>& command)
    {
        // Deferred like everything else, and for the same reason. The point of Run is to reach
        // the raw VkCommandBuffer — a library with a renderer of its own records its draws through
        // it — and running the lambda here would emit those calls at *record* time, ahead of the
        // entire recorded frame, instead of at the position they were written: an overlay recorded
        // this way would draw first, and the scene would then paint over it.
        //
        // Koral commands recorded from inside the lambda still work: Enqueue() sees _emitting
        // and runs them in place, preserving order. They are past barrier resolution by then,
        // though, so anything needing synchronisation must say so with an explicit Barrier().
        return defer("Run", [this, command] { command(*this); });
    }

    kor::VoidResult CommandBuffer::DoSubmit(const kor::SubmitInfo& info)
    {
        // Code without a frame — a headless job, a tool — still needs deferred destruction to
        // drain somewhere; submitting is the one thing it is sure to keep doing.
        detail::collectRetired();
        auto& tokens = Context::Tokens();

        std::vector<::vk::Semaphore> waitSemaphores, signalSemaphores;
        std::vector<std::uint64_t> waitValues, signalValues;
        std::vector<::vk::PipelineStageFlags> waitStages;

        for (const auto& token : info.waitFor) {
            // Nothing to hold back for — and skipping it spares the timeline a semaphore.
            if (token.Ready()) continue;
            const auto [semaphore, value] = tokens.resolve(token);
            waitSemaphores.push_back(semaphore);
            waitValues.push_back(value);
            // The token says nothing about which stage needs it, so none may start early.
            waitStages.push_back(::vk::PipelineStageFlagBits::eAllCommands);
            _inFlight.push_back(token);
        }
        for (const auto& token : info.signal) {
            if (token.Value() == 0) continue; // a default token: no event to signal
            if (token.Ready()) {
                // The GPU may only move a timeline forward; signalling where it already is, is
                // invalid Vulkan rather than a no-op.
                RecordError(ErrorCode::eInvalidArgument, std::format(
                    "Submit was asked to signal token {}, whose timeline has already reached it", token.Value()));
                continue;
            }
            const auto [semaphore, value] = tokens.resolve(token);
            signalSemaphores.push_back(semaphore);
            signalValues.push_back(value);
            _inFlight.push_back(token);
        }

        const auto commandBuffers = std::array { _handle };

        // Only timeline semaphores are signalled: the caller's tokens and the queue's epoch. (A
        // binary semaphore was signalled here once, with no waiter anywhere, which made the second
        // submit of any re-recorded buffer invalid.)
        //
        // Submit regardless of recorded errors so the fence and the tokens still signal; report the
        // first error, if any, to the caller.
        try {
            {
                const auto lock = Context::Device().lockQueues();
                const auto [epochSemaphore, epochValue] = Context::Device().nextEpoch(_queue);
                signalSemaphores.push_back(epochSemaphore);
                signalValues.push_back(epochValue);
                auto timelineInfo = ::vk::TimelineSemaphoreSubmitInfo()
                    .setWaitSemaphoreValues(waitValues)
                    .setSignalSemaphoreValues(signalValues);
                try {
                    _queue->submit(::vk::SubmitInfo()
                        .setCommandBuffers(commandBuffers)
                        .setWaitSemaphores(waitSemaphores)
                        .setWaitDstStageMask(waitStages)
                        .setSignalSemaphores(signalSemaphores)
                        .setPNext(&timelineInfo), _fence);
                    _fencePending = true;
                } catch (...) {
                    Context::Device().abandonEpoch(_queue);
                    throw;
                }
            }
            for (const auto& token : info.signal) TokenReactor::noteSubmittedSignal(token);
        } catch (const std::exception& e) {
            RecordError(ErrorCode::eBackend, e.what());
            // Nothing reached the GPU, so nothing there will signal these. Signalling them here
            // lets their waiters see the error rather than wait for ever.
            for (const auto& token : info.signal)
                if (token.Value() != 0 && !token.Ready()) token.Signal();
        }
        return Outcome();
    }

    void CommandBuffer::DoReset()
    {
        _state = {};
        _handle.reset();
    }

    void CommandBuffer::DoWaitForFence() const
    {
        // Nothing submitted with it since the last wait: it would never signal.
        if (!_fencePending) return;
        _fencePending = false;
        try {
            auto result = Context::Device()->waitForFences(_fence, true, WholeSize);
            if (result != ::vk::Result::eSuccess) {
                std::cerr << "Failed to wait for fence: " << ::vk::to_string(result) << std::endl;
            }

            Context::Device()->resetFences(_fence);
        } catch (const std::runtime_error& e) {
            std::cerr << e.what() << std::endl;
        }
    }

    void CommandBuffer::DoWriteTimerTimestamp(const kor::u32 queryIndex)
    {
        if (!_timerPool) return;
        // An even slot opens a scope and an odd one closes it. Timestamping the *earliest* stage on
        // the way in and the *latest* on the way out is what makes the pair bracket the work rather
        // than sample two arbitrary points inside it: the write happens once everything before it
        // has reached that stage, so top-of-pipe going in cannot land after the scope's first
        // command started, and bottom-of-pipe coming out cannot land before its last one finished.
        const auto stage = (queryIndex % 2 == 0)
            ? ::vk::PipelineStageFlagBits::eTopOfPipe
            : ::vk::PipelineStageFlagBits::eBottomOfPipe;
        _handle.writeTimestamp(stage, _timerPool, queryIndex);
    }

    bool CommandBuffer::DoReadTimerTimestamps(const kor::u32 scopeCount, std::vector<double>& millisecondsOut)
    {
        if (!_timerPool || scopeCount == 0) return false;

        const kor::u32 queryCount = scopeCount * 2;
        // Two values per query: the tick count and whether it has one yet. Asking for availability
        // instead of passing eWait is the whole point — this must never block the recording thread,
        // and a not-yet-ready result simply means the caller keeps the timings it already had.
        struct Query { kor::u64 ticks; kor::u64 available; };
        std::vector<Query> results(queryCount);

        const auto result = Context::Device()->getQueryPoolResults(
            _timerPool, 0, queryCount,
            results.size() * sizeof(Query), results.data(), sizeof(Query),
            ::vk::QueryResultFlagBits::e64 | ::vk::QueryResultFlagBits::eWithAvailability);

        if (result != ::vk::Result::eSuccess) return false;   // eNotReady, most often
        for (const auto& [ticks, available] : results) {
            if (!available) return false;
        }

        millisecondsOut.clear();
        millisecondsOut.reserve(scopeCount);
        for (kor::u32 i = 0; i < scopeCount; ++i) {
            const kor::u64 begin = results[i * 2].ticks;
            const kor::u64 end = results[i * 2 + 1].ticks;
            // Timestamps are only valid to timestampValidBits, so the counter can wrap between a
            // scope's two reads. Report zero rather than the enormous number the subtraction gives.
            const double ticks = end >= begin ? static_cast<double>(end - begin) : 0.0;
            millisecondsOut.push_back(ticks * static_cast<double>(_timestampPeriod) / 1'000'000.0);
        }
        return true;
    }

    kor::CommandBuffer& CommandBuffer::DoPushConstantBlock(const void *data, const kor::u32 size, const kor::u32 offset) {
        // The bytes, not the pointer: PushConstantBlock<T> hands us the address of a caller
        // temporary, which is long gone by the time End() emits.
        std::vector<std::byte> bytes(size);
        if (data && size) std::memcpy(bytes.data(), data, size);
        return defer("PushConstantBlock", [this, bytes = std::move(bytes), size, offset] {
            const void* data = bytes.data();
            if (_state.boundComputePipeline.has_value()) {
            const auto& vkPipeline = dynamic_cast<const kor::vk::ComputePipeline&>(*_state.boundComputePipeline.value());
            _handle.pushConstants(
                vkPipeline.getPipelineLayout(),
                ::vk::ShaderStageFlagBits::eCompute,
                offset,
                size,
                data);
        } else if (_state.boundGraphicsPipeline.has_value()) {
            const auto& vkPipeline = dynamic_cast<const kor::vk::GraphicsPipeline&>(*_state.boundGraphicsPipeline.value());
            _handle.pushConstants(
                vkPipeline.getPipelineLayout(),
                getVkShaderStageFlags(vkPipeline.PushConstantRange(offset).stages),
                offset,
                size,
                data);
        } else if (_state.boundRayTracingPipeline.has_value()) {
            const auto& vkPipeline = dynamic_cast<const kor::vk::RayTracingPipeline&>(*_state.boundRayTracingPipeline.value());
            _handle.pushConstants(
                vkPipeline.getPipelineLayout(),
                getVkShaderStageFlags(vkPipeline.PushConstantRange(offset).stages),
                offset,
                size,
                data);
            } else {
                std::cerr << "No pipeline bound when trying to push constants" << std::endl;
            }
        });
    }
}
