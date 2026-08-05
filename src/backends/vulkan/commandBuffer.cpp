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

    CommandBuffer::CommandBuffer(const kor::vk::Queue& queue, const ::vk::CommandBuffer commandBuffer, const ::vk::CommandPool& parentCommandPool)
        : kor::CommandBuffer(getCommandBufferUsage(queue)), _queue(queue), _parentPool(parentCommandPool) {
        _handle = commandBuffer;
        _fence = kor::vk::Context::Device()->createFence({});

        // Timestamps are not universal: a queue family may report zero valid timestamp bits, which
        // is the driver saying this queue cannot be timed. Leaving the period at zero is what makes
        // supportsTimers() false and turns the timer commands into no-ops on such a queue.
        if (queue.getFamily().getProperties().timestampValidBits > 0) {
            _timestampPeriod = Context::Runtime().getPhysicalDevice().getProperties().limits.timestampPeriod;
            if (_timestampPeriod > 0.f) {
                _timerPool = Context::Device()->createQueryPool(::vk::QueryPoolCreateInfo()
                    .setQueryType(::vk::QueryType::eTimestamp)
                    .setQueryCount(MaxTimerScopes * 2));
            }
        }
    }

    CommandBuffer::~CommandBuffer() {
        Context::Device().freeCommandBuffer(*this);

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
            _queue->submit(submitInfo, _fence);
        } catch (const std::runtime_error& e) {
            std::cerr << e.what() << std::endl;
        }
    }

    kor::CommandBuffer& CommandBuffer::Begin()
    {
        resetErrors();
        clearRecords();
        // Before the pool is reset below, which is what destroys the results being collected.
        // Re-recording is proof the GPU is done with the last submission, so this is the earliest
        // moment the previous frame's timestamps can be read — and the reason they are read here.
        retireTimers();
        constexpr auto commandBufferBeginInfo = ::vk::CommandBufferBeginInfo()
            .setFlags(::vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
        _handle.begin(commandBufferBeginInfo);
        return *this;
    }

    void CommandBuffer::End()
    {
        // Nothing recorded so far has reached the GPU. Work out where the barriers belong now
        // that the whole sequence is visible, then emit everything in order.
        resolveBarriers();

        // A timestamp may only be written into a query that has been reset, and vkCmdResetQueryPool
        // is illegal inside a render pass. Here is the one point that satisfies both without
        // guessing: the recording is complete, so the exact number of scopes is known, and not a
        // single command has been emitted yet, so we are outside every pass the frame will open.
        // Sized to what was actually recorded, which is why a frame that opens no scope resets
        // nothing at all.
        if (const glm::u32 scopes = timerScopeCount(); _timerPool && scopes > 0)
            _handle.resetQueryPool(_timerPool, 0, scopes * 2);

        submitTimers();
        emitRecords();
        _handle.end();
    }

    kor::CommandBuffer& CommandBuffer::BeginDebugLabel(const std::string& label, const glm::vec4 color)
    {
        return defer("BeginDebugLabel", [=, this] {
            // Guard on the loaded function pointer: VK_EXT_debug_utils is optional, so the
            // dispatcher entry is null when the instance was created without it.
            if (VULKAN_HPP_DEFAULT_DISPATCHER.vkCmdBeginDebugUtilsLabelEXT) {
                const auto info = ::vk::DebugUtilsLabelEXT()
                    .setPLabelName(label.c_str())
                    .setColor(std::array<float, 4>{ color.r, color.g, color.b, color.a });
                _handle.beginDebugUtilsLabelEXT(info);
            }
        });
    }

    kor::CommandBuffer& CommandBuffer::EndDebugLabel()
    {
        return defer("EndDebugLabel", [=, this] {
            if (VULKAN_HPP_DEFAULT_DISPATCHER.vkCmdEndDebugUtilsLabelEXT) {
                _handle.endDebugUtilsLabelEXT();
            }
        });
    }

    kor::CommandBuffer& CommandBuffer::InsertDebugLabel(const std::string& label, const glm::vec4 color)
    {
        return defer("InsertDebugLabel", [=, this] {
            if (VULKAN_HPP_DEFAULT_DISPATCHER.vkCmdInsertDebugUtilsLabelEXT) {
                const auto info = ::vk::DebugUtilsLabelEXT()
                    .setPLabelName(label.c_str())
                    .setColor(std::array<float, 4>{ color.r, color.g, color.b, color.a });
                _handle.insertDebugUtilsLabelEXT(info);
            }
        });
    }

    kor::CommandBuffer & CommandBuffer::BeginRendering(RenderParameters renderParameters) {
        kor::CommandBuffer::BeginRendering();
        const auto framebuffer = _state.boundFramebuffer.value();
        return kor::CommandBuffer::BeginRendering(framebuffer, renderParameters);
    }

    kor::CommandBuffer& CommandBuffer::doBeginRendering(kor::ResourceRef<const kor::Framebuffer> framebuffer, RenderParameters renderParameters)
    {
        stateBeginRendering(framebuffer);
        // The attachment transitions are declared as uses by kor::CommandBuffer::BeginRendering
        // and emitted by the resolver *before* this record — they cannot be emitted here, since
        // by then the render pass is about to open and Vulkan forbids a transition inside one.

        std::vector<::vk::RenderingAttachmentInfoKHR> colorAttachmentInfos;
        int i = 0;
        for (auto& colorAttachment : framebuffer->getColorAttachments()) {
            auto attachInfo = ::vk::RenderingAttachmentInfoKHR()
                .setImageView(**dynamic_cast<const kor::vk::ImageView*>(&colorAttachment.get()))
                .setImageLayout(::vk::ImageLayout::eColorAttachmentOptimal)
                .setClearValue(getVkClearValue(framebuffer->getClearColor(i)))
                .setLoadOp(getVkLoadOp(renderParameters.colorLoadOperation))
                .setStoreOp(getVkStoreOp(renderParameters.colorStoreOperation));
            if (framebuffer->hasResolveAttachments()) {
                attachInfo
                    .setResolveImageLayout(::vk::ImageLayout::eColorAttachmentOptimal)
                    .setResolveImageView(**dynamic_cast<const kor::vk::ImageView*>(&framebuffer->getResolveAttachment(i)))
                    .setResolveMode(getVkResolveMode(framebuffer->getResolveMode()));
            }
            colorAttachmentInfos.push_back(attachInfo);
            i++;
        }

        const auto depthAttachment = framebuffer->hasDepthAttachment() ? std::optional(::vk::RenderingAttachmentInfoKHR()
            .setImageView(**dynamic_cast<const kor::vk::ImageView*>(&framebuffer->getDepthAttachment()))
            .setImageLayout(::vk::ImageLayout::eDepthStencilAttachmentOptimal)
            .setClearValue(::vk::ClearValue().setDepthStencil({ framebuffer->getClearDepth(), static_cast<glm::u32>(framebuffer->getClearStencil()) }))
            .setLoadOp(getVkLoadOp(renderParameters.depthLoadOperation))
            .setStoreOp(getVkStoreOp(renderParameters.depthStoreOperation))) : std::nullopt;

        const auto stencilAttachment = framebuffer->hasStencilAttachment() ? std::optional(::vk::RenderingAttachmentInfoKHR()
            .setImageView(**dynamic_cast<const kor::vk::ImageView*>(&framebuffer->getStencilAttachment()))
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

    kor::CommandBuffer& CommandBuffer::EndRendering()
    {
        kor::CommandBuffer::EndRendering();
        return defer("EndRendering", [=, this] {
            _handle.endRenderingKHR();
        }, PassEdge::eCloses);
    }

    kor::CommandBuffer& CommandBuffer::SetViewport(glm::u32 x, glm::u32 y, glm::u32 width, glm::u32 height)
    {
        kor::CommandBuffer::SetViewport(x, y, width, height);
        return defer("SetViewport", [=, this] {
            // Koral's canonical clip space is Vulkan's own, so the viewport is passed straight
            // through for every framebuffer — no negative height, no default/offscreen split.
            // OpenGL is what adapts (see ogl Scheduler::Initialize).
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

    kor::CommandBuffer& CommandBuffer::SetScissor(glm::u32 x, glm::u32 y, glm::u32 width, glm::u32 height)
    {
        kor::CommandBuffer::SetScissor(x, y, width, height);
        return defer("SetScissor", [=, this] {
            const ::vk::Rect2D scissor = ::vk::Rect2D()
                .setOffset({ static_cast<glm::i32>(x), static_cast<glm::i32>(y) })
                .setExtent({ width, height });
            _handle.setScissor(0, scissor);
        });
    }

    kor::CommandBuffer& CommandBuffer::SetLineWidth(const float lineWidth)
    {
        kor::CommandBuffer::SetLineWidth(lineWidth);
        return defer("SetLineWidth", [=, this] {
            _handle.setLineWidth(lineWidth);
        });
    }

    kor::CommandBuffer& CommandBuffer::SetDepthBias(const float constantFactor, const float clamp, const float slopeFactor)
    {
        kor::CommandBuffer::SetDepthBias(constantFactor, clamp, slopeFactor);
        return defer("SetDepthBias", [=, this] {
            _handle.setDepthBias(constantFactor, clamp, slopeFactor);
        });
    }

    kor::CommandBuffer& CommandBuffer::SetBlendConstants(const glm::vec4 constants)
    {
        kor::CommandBuffer::SetBlendConstants(constants);
        return defer("SetBlendConstants", [=, this] {
            const float bc[4] = { constants.r, constants.g, constants.b, constants.a };
            _handle.setBlendConstants(bc);
        });
    }

    kor::CommandBuffer& CommandBuffer::SetStencilCompareMask(const StencilFace face, const glm::u32 compareMask)
    {
        kor::CommandBuffer::SetStencilCompareMask(face, compareMask);
        return defer("SetStencilCompareMask", [=, this] {
            _handle.setStencilCompareMask(getVkStencilFace(face), compareMask);
        });
    }

    kor::CommandBuffer& CommandBuffer::SetStencilWriteMask(const StencilFace face, const glm::u32 writeMask)
    {
        kor::CommandBuffer::SetStencilWriteMask(face, writeMask);
        return defer("SetStencilWriteMask", [=, this] {
            _handle.setStencilWriteMask(getVkStencilFace(face), writeMask);
        });
    }

    kor::CommandBuffer& CommandBuffer::SetStencilReference(const StencilFace face, const glm::u32 reference)
    {
        kor::CommandBuffer::SetStencilReference(face, reference);
        return defer("SetStencilReference", [=, this] {
            _handle.setStencilReference(getVkStencilFace(face), reference);
        });
    }

    kor::CommandBuffer& CommandBuffer::SetCullMode(const Flags<CullMode> cullMode)
    {
        kor::CommandBuffer::SetCullMode(cullMode);
        return defer("SetCullMode", [=, this] {
            _handle.setCullMode(getVkCullMode(cullMode));
        });
    }

    kor::CommandBuffer& CommandBuffer::SetFrontFace(const FrontFace frontFace)
    {
        kor::CommandBuffer::SetFrontFace(frontFace);
        return defer("SetFrontFace", [=, this] {
            // Winding is canonical (Vulkan) too, so this is a plain pass-through. GL agrees
            // because GL_UPPER_LEFT negates NDC Y, which flips its window-space winding to
            // match — see ogl Scheduler::Initialize.
            _handle.setFrontFace(getVkFrontFace(frontFace));
        });
    }

    kor::CommandBuffer& CommandBuffer::SetDepthTestEnable(const bool enable)
    {
        kor::CommandBuffer::SetDepthTestEnable(enable);
        return defer("SetDepthTestEnable", [=, this] {
            _handle.setDepthTestEnable(enable);
        });
    }

    kor::CommandBuffer& CommandBuffer::SetDepthWriteEnable(const bool enable)
    {
        kor::CommandBuffer::SetDepthWriteEnable(enable);
        return defer("SetDepthWriteEnable", [=, this] {
            _handle.setDepthWriteEnable(enable);
        });
    }

    kor::CommandBuffer& CommandBuffer::SetDepthCompareOp(const CompareOp compareOp)
    {
        kor::CommandBuffer::SetDepthCompareOp(compareOp);
        return defer("SetDepthCompareOp", [=, this] {
            _handle.setDepthCompareOp(getVkCompareOp(compareOp));
        });
    }

    kor::CommandBuffer& CommandBuffer::SetStencilTestEnable(const bool enable)
    {
        kor::CommandBuffer::SetStencilTestEnable(enable);
        return defer("SetStencilTestEnable", [=, this] {
            _handle.setStencilTestEnable(enable);
        });
    }

    kor::CommandBuffer& CommandBuffer::SetStencilOp(const StencilFace face, const StencilOp failOp, const StencilOp passOp, const StencilOp depthFailOp, const CompareOp compareOp)
    {
        kor::CommandBuffer::SetStencilOp(face, failOp, passOp, depthFailOp, compareOp);
        return defer("SetStencilOp", [=, this] {
            _handle.setStencilOp(getVkStencilFace(face), getVkStencilOp(failOp), getVkStencilOp(passOp), getVkStencilOp(depthFailOp), getVkCompareOp(compareOp));
        });
    }

    kor::CommandBuffer& CommandBuffer::SetDepthBiasEnable(const bool enable)
    {
        kor::CommandBuffer::SetDepthBiasEnable(enable);
        return defer("SetDepthBiasEnable", [=, this] {
            _handle.setDepthBiasEnable(enable);
        });
    }

    kor::CommandBuffer& CommandBuffer::SetRasterizerDiscardEnable(const bool enable)
    {
        kor::CommandBuffer::SetRasterizerDiscardEnable(enable);
        return defer("SetRasterizerDiscardEnable", [=, this] {
            _handle.setRasterizerDiscardEnable(enable);
        });
    }

    kor::CommandBuffer& CommandBuffer::SetPrimitiveRestartEnable(const bool enable)
    {
        kor::CommandBuffer::SetPrimitiveRestartEnable(enable);
        return defer("SetPrimitiveRestartEnable", [=, this] {
            _handle.setPrimitiveRestartEnable(enable);
        });
    }

    kor::CommandBuffer& CommandBuffer::doBindComputePipeline(kor::ResourceRef<const kor::ComputePipeline> pipeline)
    {
        stateBindComputePipeline(pipeline);
        pipeline->Bind(*this);
        return *this;
    }

    kor::CommandBuffer& CommandBuffer::doBindGraphicsPipeline(kor::ResourceRef<const kor::GraphicsPipeline> pipeline)
    {
        stateBindGraphicsPipeline(pipeline);
        pipeline->Bind(*this);
        // Dynamic state (front face included) is applied lazily before the first draw
        // via applyDynamicDefaults(), so nothing to emit here.
        return *this;
    }

    kor::CommandBuffer& CommandBuffer::doBindRayTracingPipeline(kor::ResourceRef<const kor::RayTracingPipeline> pipeline)
    {
        stateBindRayTracingPipeline(pipeline);
        pipeline->Bind(*this);
        return *this;
    }

    kor::CommandBuffer& CommandBuffer::TraceRays(const glm::u32 width, const glm::u32 height, const glm::u32 depth, const std::source_location where)
    {
        if (_failed) return *this;
        if (!_state.boundRayTracingPipeline.has_value())
            return record(ErrorCode::eNoRayTracingPipelineBound, "Cannot trace rays without a ray-tracing pipeline bound.");

        return deferAt("TraceRays", where, [=, this] {
            const auto& vkPipeline = dynamic_cast<const kor::vk::RayTracingPipeline&>(*_state.boundRayTracingPipeline.value());
            _handle.traceRaysKHR(
                vkPipeline.getRaygenRegion(),
                vkPipeline.getMissRegion(),
                vkPipeline.getHitRegion(),
                vkPipeline.getCallableRegion(),
                width, height, depth);
        }, PassEdge::eNone, usesForBoundResources(false), boundPipelineUsesDeviceAddresses());
    }

    kor::CommandBuffer& CommandBuffer::doBindDescriptorSet(const glm::u32 index, kor::ResourceRef<const kor::DescriptorSet> set, const bool debug)
    {
        if (debug) set->DebugPrint();

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

    kor::CommandBuffer& CommandBuffer::doBindMesh(kor::ResourceRef<const kor::Mesh> mesh)
    {
        stateBindMesh(mesh);
        std::vector<::vk::Buffer> vertexBuffers;
        std::vector<::vk::DeviceSize> offsets;
        for (const auto& buffer : mesh->getVertexBuffers()) {
            const auto& vkBuffer = dynamic_cast<const kor::vk::Buffer&>(*buffer);
            vertexBuffers.push_back(*vkBuffer);
            offsets.push_back(0);
        }
        _handle.bindVertexBuffers(0, vertexBuffers, offsets);
        if (mesh->hasIndexBuffer())
        {
            const auto& vkBuffer = dynamic_cast<const kor::vk::Buffer&>(*mesh->getIndexBuffer().value());
            _handle.bindIndexBuffer(*vkBuffer, 0, getVkIndexType(mesh->getIndexType().value()));
        }
        return *this;
    }

    kor::CommandBuffer & CommandBuffer::doBarrier(
        const std::vector<kor::BufferBarrier> bufferBarriers,
        const std::vector<kor::ImageBarrier> imageBarriers) {

        ::vk::PipelineStageFlags srcStageMask = ::vk::PipelineStageFlagBits::eAllCommands;
        ::vk::PipelineStageFlags dstStageMask = ::vk::PipelineStageFlagBits::eNone;

        std::vector<::vk::BufferMemoryBarrier> vkBufferBarriers;
        std::vector<::vk::ImageMemoryBarrier> vkImageBarriers;
        for (const auto& barrier : bufferBarriers) {
            const auto& vkBuffer = dynamic_cast<const kor::vk::Buffer&>(*barrier.getBuffer());
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
            const auto& vkImage = dynamic_cast<const kor::vk::Image&>(*barrier.getImage());

            const auto newLayout = getVkImageLayout(barrier.getDstAccess());
            const auto dstAccessMask = getVkAccessFlags(barrier.getDstAccess());
            const auto aspectMask = getVkImageAspectFlags(vkImage.getFormat());

            // An absent count means "the rest of the image", so it is measured from the base
            // rather than from zero. Resolving it to the image's *total* count instead walked past
            // the last level whenever a base was given without one. Matches resolveBarriers().
            const auto baseMip = barrier.getBaseMipLevel().value_or(0);
            const auto mipCount = barrier.getLevelCount().value_or(vkImage.getMipLevels() - baseMip);
            const auto baseLayer = barrier.getBaseArrayLayer().value_or(0);
            const auto layerCount = barrier.getLayerCount().value_or(vkImage.getArrayLayers() - baseLayer);

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

    kor::CommandBuffer& CommandBuffer::Dispatch(const glm::u32 groupCountX, const glm::u32 groupCountY, const glm::u32 groupCountZ, const std::source_location where)
    {
        if (_failed) return *this;
        return deferAt("Dispatch", where, [=, this] {
            _handle.dispatch(groupCountX, groupCountY, groupCountZ);
        }, PassEdge::eNone, usesForBoundResources(false), boundPipelineUsesDeviceAddresses());
    }

    kor::CommandBuffer & CommandBuffer::doDispatchIndirect(kor::ResourceRef<const kor::Buffer> indirectBuffer, glm::u64 offset) {
        if (_failed) return *this;
        const auto& vkBuffer = dynamic_cast<const kor::vk::Buffer&>(*indirectBuffer);
        _handle.dispatchIndirect(*vkBuffer, offset);
        return *this;
    }

    kor::CommandBuffer& CommandBuffer::DrawMeshTasks(const glm::u32 taskCountX, const glm::u32 taskCountY, const glm::u32 taskCountZ, const std::source_location where) {
        kor::CommandBuffer::DrawMeshTasks(taskCountX, taskCountY, taskCountZ, where);
        if (_failed) return *this;
        return deferAt("DrawMeshTasks", where, [=, this] {
            applyDynamicDefaults();
            _handle.drawMeshTasksEXT(taskCountX, taskCountY, taskCountZ);
        }, PassEdge::eNone, usesForBoundResources(true), boundPipelineUsesDeviceAddresses());
    }

    kor::CommandBuffer & CommandBuffer::doDrawIndirect(kor::ResourceRef<const kor::Buffer> indirectBuffer, glm::u64 offset, glm::u32 drawCount, glm::u32 stride) {
        if (_failed) return *this;
        const auto& vkBuffer = dynamic_cast<const kor::vk::Buffer&>(*indirectBuffer);
        applyDynamicDefaults();
        _handle.drawIndirect(*vkBuffer, offset, drawCount, stride);
        return *this;
    }

    kor::CommandBuffer & CommandBuffer::doDrawIndexedIndirect(kor::ResourceRef<const kor::Buffer> indirectBuffer, glm::u64 offset, glm::u32 drawCount, glm::u32 stride) {
        if (_failed) return *this;
        const auto& vkBuffer = dynamic_cast<const kor::vk::Buffer&>(*indirectBuffer);
        applyDynamicDefaults();
        _handle.drawIndexedIndirect(*vkBuffer, offset, drawCount, stride);
        return *this;
    }

    kor::CommandBuffer & CommandBuffer::doDrawMeshTasksIndirect(kor::ResourceRef<const kor::Buffer> indirectBuffer, glm::u64 offset, glm::u32 drawCount, glm::u32 stride) {
        if (_failed) return *this;
        const auto& vkBuffer = dynamic_cast<const kor::vk::Buffer&>(*indirectBuffer);
        applyDynamicDefaults();
        _handle.drawMeshTasksIndirectEXT(*vkBuffer, offset, drawCount, stride);
        return *this;
    }

    kor::CommandBuffer& CommandBuffer::Draw(glm::u64 vertexCount, const glm::u32 instanceCount, const glm::u32 firstVertex, const glm::u32 firstInstance, const std::source_location where)
    {
        if (vertexCount == UINT64_MAX && _state.boundMesh.has_value()) {
            vertexCount = _state.boundMesh.value()->getVertexCount();
        }
        kor::CommandBuffer::Draw(vertexCount, instanceCount, firstVertex, firstInstance, where);
        if (_failed) return *this;
        return deferAt("Draw", where, [=, this] {
            // At emit time the tracked state has replayed to this point, so the dynamic-state
            // mask is the one that was in force for *this* draw, not the end of recording.
            applyDynamicDefaults();
            _handle.draw(vertexCount, instanceCount, firstVertex, firstInstance);
        }, PassEdge::eNone, usesForBoundResources(true), boundPipelineUsesDeviceAddresses());
    }

    kor::CommandBuffer & CommandBuffer::DrawIndexed(glm::u64 indexCount, glm::u32 instanceCount, glm::u32 firstIndex, glm::i32 vertexOffset, glm::u32 firstInstance, const std::source_location where) {
        kor::CommandBuffer::DrawIndexed(indexCount, instanceCount, firstIndex, vertexOffset, firstInstance, where);
        if (_failed) return *this;
        const auto mesh = _state.boundMesh.value();
        if (indexCount == UINT64_MAX) {
            indexCount = mesh->getIndexCount().value();
        }
        return deferAt("DrawIndexed", where, [=, this] {
            applyDynamicDefaults();
            _handle.drawIndexed(indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
        }, PassEdge::eNone, usesForBoundResources(true), boundPipelineUsesDeviceAddresses());
    }

    kor::CommandBuffer & CommandBuffer::doClearBuffer(kor::ResourceRef<const kor::Buffer> buffer, glm::u64 offset, glm::u64 size) {
        const auto& vkBuffer = dynamic_cast<const kor::vk::Buffer&>(*buffer);
        _handle.fillBuffer(*vkBuffer, offset, size, 0);
        return *this;
    }

    kor::CommandBuffer & CommandBuffer::doFillBuffer(kor::ResourceRef<const kor::Buffer> buffer, void *data, const glm::u64 offset, glm::u64 size) {
        const auto& vkBuffer = dynamic_cast<const kor::vk::Buffer&>(*buffer);
        if (size == UINT64_MAX) {
            size = vkBuffer.getSize() - offset;
        }
        _handle.updateBuffer(*vkBuffer, offset, size, data);
        return *this;
    }

    kor::CommandBuffer & CommandBuffer::doCopyBuffer(ResourceRef<const kor::Buffer> srcBuffer, ResourceRef<const kor::Buffer> dstBuffer, glm::u64 size, const glm::u64 srcOffset, const glm::u64 dstOffset) {
        if (_failed) return *this;
        const auto& vkSrcBuffer = dynamic_cast<const kor::vk::Buffer&>(*srcBuffer);
        const auto& vkDstBuffer = dynamic_cast<const kor::vk::Buffer&>(*dstBuffer);
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

    kor::CommandBuffer& CommandBuffer::doClearColorImage(kor::ResourceRef<const kor::Image> image, const glm::vec4 color) {
        const auto& vkImage = dynamic_cast<const kor::vk::Image&>(*image);

        ::vk::ClearValue clearValue;
        clearValue.color = ::vk::ClearColorValue(std::array<float, 4>{ color.r, color.g, color.b, color.a });
        vkImage.Clear(*this, clearValue);
        return *this;
    }

    kor::CommandBuffer& CommandBuffer::doBlit(ResourceRef<const kor::Image> srcImage, kor::Blit blitInfo) {
        ResourceRef<const kor::Image> dstImage =  dynamic_cast<const Scheduler&>(kor::Context::Scheduler()).getSwapChain().getImage();
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

    kor::CommandBuffer& CommandBuffer::doBlit(kor::ResourceRef<const kor::Image> srcImage, kor::ResourceRef<const kor::Image> dstImage, kor::Blit blitInfo)
    {
        if (blitInfo.srcExtent == glm::ivec3(-1))
            blitInfo.srcExtent = srcImage->getExtent();
        if (blitInfo.dstExtent == glm::ivec3(-1))
            blitInfo.dstExtent = dstImage->getExtent();

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

    kor::CommandBuffer & CommandBuffer::doResolve(ResourceRef<const kor::Image> srcImage, kor::Resolve resolveInfo) {
        if (_failed) return *this;
        if (srcImage->getSampleCount() == SampleCount::e1)
            return record(ErrorCode::eResolveRequiresMultisample, "Resolve source image must be multisampled.");

        if (!_resolveHelperImage)
            _resolveHelperImage = kor::Image::Builder()
                .setIsPerFrame(true)
                .setExtent(srcImage->getExtent())
                .setFormat(srcImage->getFormat())
                .setUsage(kor::Image::Usage::eTransferDst)
                .addUsage(kor::Image::Usage::eTransferSrc)
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

        Blit(kor::ResourceRef<const kor::Image>(_resolveHelperImage), kor::Blit{});
        return *this;
    }

    kor::CommandBuffer& CommandBuffer::doResolve(kor::ResourceRef<const kor::Image> srcImage, kor::ResourceRef<const kor::Image> dstImage, kor::Resolve resolveInfo) {
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

    kor::CommandBuffer& CommandBuffer::doCopyBufferToImage(ResourceRef<const kor::Buffer> buffer, ResourceRef<const kor::Image> image, kor::Copy copyInfo) {
        if (_failed) return *this;
        const auto& vkBuffer = dynamic_cast<const kor::vk::Buffer&>(*buffer);
        const auto& vkImage = dynamic_cast<const kor::vk::Image&>(*image);

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

        // A compressed format is addressed in blocks, and Vulkan requires the buffer's row length and
        // image height to be whole numbers of them. The last mip levels of any texture are smaller
        // than one block — a 2x2 level of a 4x4 format — so rounding up here is not an edge case but
        // the ordinary end of every mip chain.
        if (Image::IsBlockCompressed(image->getFormat())) {
            const auto block = Image::BlockExtentFromImageFormat(image->getFormat());
            const auto roundUp = [](const glm::u32 value, const glm::u32 to) { return (value + to - 1) / to * to; };
            copyInfo.bufferRowLength = roundUp(copyInfo.bufferRowLength, block.x);
            copyInfo.bufferImageHeight = roundUp(copyInfo.bufferImageHeight, block.y);
        }
        if (copyInfo.bufferRowLength < copyInfo.imageExtent.x || copyInfo.bufferImageHeight < copyInfo.imageExtent.y)
            return record(ErrorCode::eInvalidArgument,
                std::format("Buffer row length {} / image height {} too small for image extent {}x{}.",
                            copyInfo.bufferRowLength, copyInfo.bufferImageHeight, copyInfo.imageExtent.x, copyInfo.imageExtent.y));
        // Copy footprint in *bytes* (not texels): rowLength/imageHeight give the packed extent, and
        // SizeOfRegion turns it into bytes — counting blocks for a compressed format and texels for
        // an uncompressed one, so a BC7 upload is measured in the units it is actually stored in
        // rather than in texels it does not have. For packed (unpadded) buffers this is the exact
        // required size. Depth/stencil texel sizes are conservatively over-estimated, which only
        // makes the guard stricter.
        {
            const glm::u64 copyBytes = Image::SizeOfRegion(
                image->getFormat(),
                { copyInfo.bufferRowLength, copyInfo.bufferImageHeight, copyInfo.imageExtent.z },
                copyInfo.imageLayerCount);
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

    kor::CommandBuffer & CommandBuffer::doCopyImageToBuffer(ResourceRef<const kor::Image> image, ResourceRef<const kor::Buffer> buffer, kor::Copy copyInfo) {
        if (_failed) return *this;
        const auto& vkBuffer = dynamic_cast<const kor::vk::Buffer&>(*buffer);
        const auto& vkImage = dynamic_cast<const kor::vk::Image&>(*image);

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

        // A compressed format is addressed in blocks, and Vulkan requires the buffer's row length and
        // image height to be whole numbers of them. The last mip levels of any texture are smaller
        // than one block — a 2x2 level of a 4x4 format — so rounding up here is not an edge case but
        // the ordinary end of every mip chain.
        if (Image::IsBlockCompressed(image->getFormat())) {
            const auto block = Image::BlockExtentFromImageFormat(image->getFormat());
            const auto roundUp = [](const glm::u32 value, const glm::u32 to) { return (value + to - 1) / to * to; };
            copyInfo.bufferRowLength = roundUp(copyInfo.bufferRowLength, block.x);
            copyInfo.bufferImageHeight = roundUp(copyInfo.bufferImageHeight, block.y);
        }
        if (copyInfo.bufferRowLength < copyInfo.imageExtent.x || copyInfo.bufferImageHeight < copyInfo.imageExtent.y)
            return record(ErrorCode::eInvalidArgument,
                std::format("Buffer row length {} / image height {} too small for image extent {}x{}.",
                            copyInfo.bufferRowLength, copyInfo.bufferImageHeight, copyInfo.imageExtent.x, copyInfo.imageExtent.y));
        // Copy footprint in *bytes* (not texels): rowLength/imageHeight give the packed extent, and
        // SizeOfRegion turns it into bytes — counting blocks for a compressed format and texels for
        // an uncompressed one, so a BC7 upload is measured in the units it is actually stored in
        // rather than in texels it does not have. For packed (unpadded) buffers this is the exact
        // required size. Depth/stencil texel sizes are conservatively over-estimated, which only
        // makes the guard stricter.
        {
            const glm::u64 copyBytes = Image::SizeOfRegion(
                image->getFormat(),
                { copyInfo.bufferRowLength, copyInfo.bufferImageHeight, copyInfo.imageExtent.z },
                copyInfo.imageLayerCount);
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

    kor::CommandBuffer& CommandBuffer::Run(const std::function<void(kor::CommandBuffer&)>& command)
    {
        // Deferred like everything else, and for the same reason. The point of Run is to reach
        // the raw VkCommandBuffer — the ImGui backend records its own draws through it — and
        // running the lambda here would emit those calls at *record* time, ahead of the entire
        // recorded frame, instead of at the position they were written. That is what stopped the
        // GUI appearing: it drew first and the scene then painted over it.
        //
        // The OpenGL backend's Run has always enqueued, so this also makes the two agree.
        // Koral commands recorded from inside the lambda still work: enqueue() sees _emitting
        // and runs them in place, preserving order. They are past barrier resolution by then,
        // though, so anything needing synchronisation must say so with an explicit Barrier() —
        // which is exactly what GUI::Render does.
        return defer("Run", [this, command] { command(*this); });
    }

    kor::VoidResult CommandBuffer::Submit()
    {
        const auto commandBuffers = std::array { _handle };
        const auto submitInfo = ::vk::SubmitInfo()
            .setCommandBuffers(commandBuffers);

        // Nothing is signalled but the fence. A semaphore was signalled here too, with no waiter
        // anywhere, which made the second submit of any re-recorded buffer invalid: a binary
        // semaphore must be unsignalled when the signal operation executes, and nothing was
        // consuming it to get it back there.
        //
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

    void CommandBuffer::writeTimerTimestamp(const glm::u32 queryIndex)
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

    bool CommandBuffer::readTimerTimestamps(const glm::u32 scopeCount, std::vector<double>& millisecondsOut)
    {
        if (!_timerPool || scopeCount == 0) return false;

        const glm::u32 queryCount = scopeCount * 2;
        // Two values per query: the tick count and whether it has one yet. Asking for availability
        // instead of passing eWait is the whole point — this must never block the recording thread,
        // and a not-yet-ready result simply means the caller keeps the timings it already had.
        struct Query { glm::u64 ticks; glm::u64 available; };
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
        for (glm::u32 i = 0; i < scopeCount; ++i) {
            const glm::u64 begin = results[i * 2].ticks;
            const glm::u64 end = results[i * 2 + 1].ticks;
            // Timestamps are only valid to timestampValidBits, so the counter can wrap between a
            // scope's two reads. Report zero rather than the enormous number the subtraction gives.
            const double ticks = end >= begin ? static_cast<double>(end - begin) : 0.0;
            millisecondsOut.push_back(ticks * static_cast<double>(_timestampPeriod) / 1'000'000.0);
        }
        return true;
    }

    kor::CommandBuffer& CommandBuffer::PushConstants(const void *data, const glm::u32 size, const glm::u32 offset) {
        // The bytes, not the pointer: PushConstants<T> hands us the address of a caller
        // temporary, which is long gone by the time End() emits.
        std::vector<std::byte> bytes(size);
        if (data && size) std::memcpy(bytes.data(), data, size);
        return defer("PushConstants", [this, bytes = std::move(bytes), size, offset] {
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
                getVkShaderStageFlags(vkPipeline.getPushConstantRange(offset).stages),
                offset,
                size,
                data);
        } else if (_state.boundRayTracingPipeline.has_value()) {
            const auto& vkPipeline = dynamic_cast<const kor::vk::RayTracingPipeline&>(*_state.boundRayTracingPipeline.value());
            _handle.pushConstants(
                vkPipeline.getPipelineLayout(),
                getVkShaderStageFlags(vkPipeline.getPushConstantRange(offset).stages),
                offset,
                size,
                data);
            } else {
                std::cerr << "No pipeline bound when trying to push constants" << std::endl;
            }
        });
    }
}
