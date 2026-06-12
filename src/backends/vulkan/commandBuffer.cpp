//
// Created by radue on 2/27/2026.
//

module;
#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1

#include <iostream>

#include "vulkanContext.h"
#include "vk_enum_conversions.h"

module vk.commandBuffer;
import gfx.flags;

namespace gfx::vk
{
    class ComputePipeline;

    Flags<CommandBuffer::Usage> getCommandBufferUsage(const gfx::vk::Queue& queue)
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
        constexpr auto commandBufferBeginInfo = ::vk::CommandBufferBeginInfo()
            .setFlags(::vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
        _handle.begin(commandBufferBeginInfo);
        return *this;
    }

    void CommandBuffer::End()
    {
        _handle.end();
    }

    gfx::CommandBuffer & CommandBuffer::BeginRendering(RenderParameters renderParameters) {
        gfx::CommandBuffer::BeginRendering();
        const auto framebuffer = _state.boundFramebuffer.value();
        return BeginRendering(framebuffer, renderParameters);
    }

    gfx::CommandBuffer& CommandBuffer::BeginRendering(gfx::ResourceRef<const gfx::Framebuffer> framebuffer, RenderParameters renderParameters)
    {
        gfx::CommandBuffer::BeginRendering(framebuffer);
        std::vector<gfx::ImageBarrier> imageBarriers;
        for (const auto& attachment : framebuffer->getColorAttachments())
        {
            imageBarriers.push_back({
                attachment->getImage(),
                ResourceAccess::ColorAttachment
            });
        }
        if (framebuffer->hasDepthAttachment())
        {
            imageBarriers.push_back({
                framebuffer->getDepthAttachment()->getImage(),
                ResourceAccess::DepthAttachment
            });
        }
        if (framebuffer->hasStencilAttachment())
        {
            imageBarriers.push_back({
                framebuffer->getStencilAttachment()->getImage(),
                ResourceAccess::StencilAttachment
            });
        }
        Barrier({}, imageBarriers);

        std::vector<::vk::RenderingAttachmentInfoKHR> colorAttachmentInfos;
        int i = 0;
        for (auto colorAttachment : framebuffer->getColorAttachments()) {
            colorAttachmentInfos.push_back(::vk::RenderingAttachmentInfoKHR()
                .setImageView(*dynamic_cast<const gfx::vk::ImageView&>(*colorAttachment))
                .setImageLayout(::vk::ImageLayout::eColorAttachmentOptimal)
                .setClearValue(getVkClearValue(framebuffer->getClearColor(i)))
                .setResolveImageLayout(framebuffer->hasResolveAttachments() ? ::vk::ImageLayout::eColorAttachmentOptimal : ::vk::ImageLayout::eUndefined)
                .setResolveImageView(framebuffer->hasResolveAttachments() ? *dynamic_cast<const gfx::vk::ImageView&>(*framebuffer->getResolveAttachment(i)) : nullptr)
                .setResolveMode(getVkResolveMode(framebuffer->getResolveMode()))
                .setLoadOp(getVkLoadOp(renderParameters.colorLoadOperation))
                .setStoreOp(getVkStoreOp(renderParameters.colorStoreOperation)));
            i++;
        }

        const auto depthAttachment = framebuffer->hasDepthAttachment() ? std::optional(::vk::RenderingAttachmentInfoKHR()
            .setImageView(*dynamic_cast<const gfx::vk::ImageView&>(*framebuffer->getDepthAttachment()))
            .setImageLayout(::vk::ImageLayout::eDepthStencilAttachmentOptimal)
            .setClearValue(::vk::ClearValue().setDepthStencil({ framebuffer->getClearDepth(), static_cast<glm::u32>(framebuffer->getClearStencil()) }))
            .setLoadOp(getVkLoadOp(renderParameters.depthLoadOperation))
            .setStoreOp(getVkStoreOp(renderParameters.depthStoreOperation))) : std::nullopt;

        const auto stencilAttachment = framebuffer->hasStencilAttachment() ? std::optional(::vk::RenderingAttachmentInfoKHR()
            .setImageView(*dynamic_cast<const gfx::vk::ImageView&>(*framebuffer->getStencilAttachment()))
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
        const bool isDefaultFramebuffer = _state.boundFramebuffer.value()->IsDefault();
        const ::vk::Viewport viewport = ::vk::Viewport()
            .setX(static_cast<float>(x))
            .setY(static_cast<float>(y) + (isDefaultFramebuffer ? static_cast<float>(height) : 0.f)) // Vulkan's viewport y-axis is inverted compared to OpenGL, so we need to add the height to the y coordinate and set the height to negative value to flip it back.
            .setWidth(static_cast<float>(width))
            .setHeight(static_cast<float>(height) * (isDefaultFramebuffer ? -1.f : 1.f)) // If it's the default framebuffer, we need to flip the height to account for the inverted y-axis. For offscreen framebuffers, we can keep it as is.
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

    gfx::CommandBuffer& CommandBuffer::BindPipeline(gfx::ResourceRef<const gfx::ComputePipeline> pipeline)
    {
        gfx::CommandBuffer::BindPipeline(pipeline);
        pipeline->Bind(*this);
        return *this;
    }

    gfx::CommandBuffer& CommandBuffer::BindPipeline(gfx::ResourceRef<const gfx::GraphicsPipeline> pipeline)
    {
        gfx::CommandBuffer::BindPipeline(pipeline);
        pipeline->Bind(*this);
        const bool isDefaultFramebuffer = _state.boundFramebuffer.has_value() && _state.boundFramebuffer.value()->IsDefault();
        const auto frontFace = pipeline->getRasterizationState().frontFace;
        _handle.setFrontFace(isDefaultFramebuffer ? frontFace == gfx::FrontFace::eClockwise ? ::vk::FrontFace::eClockwise
                                                                                            : ::vk::FrontFace::eCounterClockwise
                                                  : frontFace == gfx::FrontFace::eClockwise ? ::vk::FrontFace::eCounterClockwise
                                                                                            : ::vk::FrontFace::eClockwise);
        return *this;
    }

    gfx::CommandBuffer& CommandBuffer::BindDescriptorSet(const glm::u32 index, gfx::ResourceRef<const gfx::DescriptorSet> set, const bool debug)
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
        } else {
            std::cerr << "No pipeline bound when trying to bind descriptor set" << std::endl;
        }
        return *this;
    }

    gfx::CommandBuffer& CommandBuffer::BindMesh(gfx::ResourceRef<const gfx::Mesh> mesh)
    {
        gfx::CommandBuffer::BindMesh(mesh);
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

    gfx::CommandBuffer & CommandBuffer::Barrier(
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

            const auto oldLayout = vkImage.getImageLayout(barrier.getBaseMipLevel().value_or(0), barrier.getBaseArrayLayer().value_or(0));
            const auto newLayout = getVkImageLayout(barrier.getDstAccess());

            // std::cout << *vkImage << " " << ::vk::to_string(oldLayout) << " -> " << ::vk::to_string(newLayout) << " (mipLevel: " << barrier.getBaseMipLevel().value_or(0) << ", arrayLayer: " << barrier.getBaseArrayLayer().value_or(0) << ")" << std::endl;

            vkImageBarriers.push_back(::vk::ImageMemoryBarrier()
                .setSrcAccessMask(vkImage.getAccessMask(barrier.getBaseMipLevel().value_or(0), barrier.getBaseArrayLayer().value_or(0)))
                .setDstAccessMask(getVkAccessFlags(barrier.getDstAccess()))
                .setOldLayout(oldLayout)
                .setNewLayout(newLayout)
                .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
                .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
                .setImage(*vkImage)
                .setSubresourceRange(::vk::ImageSubresourceRange()
                    .setAspectMask(getVkImageAspectFlags(vkImage.getFormat()))
                    .setBaseMipLevel(barrier.getBaseMipLevel().value_or(0))
                    .setLevelCount(barrier.getLevelCount().value_or(vkImage.getMipLevels()))
                    .setBaseArrayLayer(barrier.getBaseArrayLayer().value_or(0))
                    .setLayerCount(barrier.getLayerCount().value_or(vkImage.getArrayLayers()))));

            dstStageMask |= getVkPipelineStageFlags(barrier.getDstAccess());

            for (auto mip = barrier.getBaseMipLevel().value_or(0); mip < (barrier.getBaseMipLevel().value_or(0) + barrier.getLevelCount().value_or(vkImage.getMipLevels())); ++mip) {
                for (auto layer = barrier.getBaseArrayLayer().value_or(0); layer < (barrier.getBaseArrayLayer().value_or(0) + barrier.getLayerCount().value_or(vkImage.getArrayLayers())); ++layer) {
                    vkImage.SetImageLayout(getVkImageLayout(barrier.getDstAccess()), mip, layer);
                    vkImage.SetAccessMask(getVkAccessFlags(barrier.getDstAccess()), mip, layer);
                }
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

    gfx::CommandBuffer& CommandBuffer::DrawMeshTasks(const glm::u32 taskCountX, const glm::u32 taskCountY, const glm::u32 taskCountZ) {
        gfx::CommandBuffer::DrawMeshTasks(taskCountX, taskCountY, taskCountZ);
        _handle.drawMeshTasksEXT(taskCountX, taskCountY, taskCountZ);
        return *this;
    }

    gfx::CommandBuffer & CommandBuffer::DrawIndirect(ResourceRef<const gfx::Buffer> indirectBuffer, glm::u64 offset, glm::u32 drawCount, glm::u32 stride) {
        gfx::CommandBuffer::DrawIndirect(indirectBuffer, offset, drawCount, stride);
        const auto& vkBuffer = dynamic_cast<const gfx::vk::Buffer&>(*indirectBuffer);
        _handle.drawIndirect(*vkBuffer, offset, drawCount, stride);
        return *this;
    }

    gfx::CommandBuffer & CommandBuffer::DrawIndexedIndirect(ResourceRef<const gfx::Buffer> indirectBuffer, glm::u64 offset, glm::u32 drawCount, glm::u32 stride) {
        gfx::CommandBuffer::DrawIndexedIndirect(indirectBuffer, offset, drawCount, stride);
        const auto& vkBuffer = dynamic_cast<const gfx::vk::Buffer&>(*indirectBuffer);
        _handle.drawIndexedIndirect(*vkBuffer, offset, drawCount, stride);
        return *this;
    }

    gfx::CommandBuffer & CommandBuffer::DrawMeshTasksIndirect(ResourceRef<const gfx::Buffer> indirectBuffer, glm::u64 offset, glm::u32 drawCount, glm::u32 stride) {
        gfx::CommandBuffer::DrawMeshTasksIndirect(indirectBuffer, offset, drawCount, stride);
        const auto& vkBuffer = dynamic_cast<const gfx::vk::Buffer&>(*indirectBuffer);
        _handle.drawMeshTasksIndirectEXT(*vkBuffer, offset, drawCount, stride);
        return *this;
    }

    gfx::CommandBuffer& CommandBuffer::Dispatch(const glm::u32 groupCountX, const glm::u32 groupCountY, const glm::u32 groupCountZ) {
        gfx::CommandBuffer::Dispatch(groupCountX, groupCountY, groupCountZ);
        _handle.dispatch(groupCountX, groupCountY, groupCountZ);
        return *this;
    }

    gfx::CommandBuffer & CommandBuffer::DispatchIndirect(ResourceRef<const gfx::Buffer> indirectBuffer, glm::u64 offset) {
        gfx::CommandBuffer::DispatchIndirect(indirectBuffer, offset);
        const auto& vkBuffer = dynamic_cast<const gfx::vk::Buffer&>(*indirectBuffer);
        _handle.dispatchIndirect(*vkBuffer, offset);
        return *this;
    }

    gfx::CommandBuffer& CommandBuffer::Draw(glm::u64 vertexCount, const glm::u32 instanceCount, const glm::u32 firstVertex, const glm::u32 firstInstance)
    {
        if (vertexCount == UINT64_MAX) {
            vertexCount = _state.boundMesh.value()->getVertexCount();
        }
        gfx::CommandBuffer::Draw(vertexCount, instanceCount, firstVertex, firstInstance);
        _handle.draw(vertexCount, instanceCount, firstVertex, firstInstance);
        return *this;
    }

    gfx::CommandBuffer & CommandBuffer::DrawIndexed(glm::u64 indexCount, glm::u32 instanceCount, glm::u32 firstIndex, glm::i32 vertexOffset, glm::u32 firstInstance) {
        gfx::CommandBuffer::DrawIndexed(indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
        const auto mesh = _state.boundMesh.value();
        if (indexCount == UINT64_MAX) {
            indexCount = mesh->getIndexCount().value();
        }
        _handle.drawIndexed(indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
        return *this;
    }

    gfx::CommandBuffer & CommandBuffer::ClearBuffer(gfx::ResourceRef<const gfx::Buffer> buffer, glm::u64 offset, glm::u64 size) {
        const auto& vkBuffer = dynamic_cast<const gfx::vk::Buffer&>(*buffer);
        _handle.fillBuffer(*vkBuffer, offset, size, 0);
        return *this;
    }

    gfx::CommandBuffer & CommandBuffer::FillBuffer(gfx::ResourceRef<const gfx::Buffer> buffer, void *data, const glm::u64 offset, glm::u64 size) {
        const auto& vkBuffer = dynamic_cast<const gfx::vk::Buffer&>(*buffer);
        if (size == UINT64_MAX) {
            size = vkBuffer.getSize() - offset;
        }
        _handle.updateBuffer(*vkBuffer, offset, size, data);
        return *this;
    }

    gfx::CommandBuffer & CommandBuffer::CopyBuffer(ResourceRef<const gfx::Buffer> srcBuffer, ResourceRef<const gfx::Buffer> dstBuffer, glm::u64 size, const glm::u64 srcOffset, const glm::u64 dstOffset) {
        const auto& vkSrcBuffer = dynamic_cast<const gfx::vk::Buffer&>(*srcBuffer);
        const auto& vkDstBuffer = dynamic_cast<const gfx::vk::Buffer&>(*dstBuffer);
        if (size == UINT64_MAX) {
            size = std::min(vkSrcBuffer.getSize() - srcOffset, vkDstBuffer.getSize() - dstOffset);
        }
        if (size > vkSrcBuffer.getSize() - srcOffset || size > vkDstBuffer.getSize() - dstOffset) {
            gfx::log::error("Copy size exceeds buffer size (srcBuffer size: {}, dstBuffer size: {}, srcOffset: {}, dstOffset: {}, copy size: {})", vkSrcBuffer.getSize(), vkDstBuffer.getSize(), srcOffset, dstOffset, size);
            throw std::runtime_error("Copy size exceeds buffer size");
        }

        _handle.copyBuffer(*vkSrcBuffer, *vkDstBuffer, ::vk::BufferCopy()
            .setSrcOffset(srcOffset)
            .setDstOffset(dstOffset)
            .setSize(size));
        return *this;
    }

    gfx::CommandBuffer& CommandBuffer::Blit(ResourceRef<const gfx::Image> srcImage, gfx::Blit blitInfo) {
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

    gfx::CommandBuffer& CommandBuffer::Blit(gfx::ResourceRef<const gfx::Image> srcImage, gfx::ResourceRef<const gfx::Image> dstImage, gfx::Blit blitInfo)
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

    gfx::CommandBuffer & CommandBuffer::Resolve(ResourceRef<const gfx::Image> srcImage, gfx::Resolve resolveInfo) {
        if (srcImage->getSampleCount() == SampleCount::e1)
            throw std::runtime_error("Source image must be multisampled for resolve operation!");

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

        Blit(_resolveHelperImage, {});
        return *this;
    }

    gfx::CommandBuffer& CommandBuffer::Resolve(gfx::ResourceRef<const gfx::Image> srcImage, gfx::ResourceRef<const gfx::Image> dstImage, gfx::Resolve resolveInfo) {
        if (srcImage->getSampleCount() == SampleCount::e1)
            throw std::runtime_error("Source image must be multisampled for resolve operation!");
        if (dstImage && dstImage->getSampleCount() != SampleCount::e1)
            throw std::runtime_error("Destination image must not be multisampled for resolve operation!");

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

    gfx::CommandBuffer& CommandBuffer::CopyBufferToImage(ResourceRef<const gfx::Buffer> buffer, ResourceRef<const gfx::Image> image, gfx::Copy copyInfo) {
        const auto& vkBuffer = dynamic_cast<const gfx::vk::Buffer&>(*buffer);
        const auto& vkImage = dynamic_cast<const gfx::vk::Image&>(*image);

        if (copyInfo.bufferOffset >= vkBuffer.getSize()) {
            gfx::log::error("Buffer offset exceeds buffer size (buffer size: {}, buffer offset: {})", vkBuffer.getSize(), copyInfo.bufferOffset);
            throw std::runtime_error("Buffer offset exceeds buffer size");
        }

        if (copyInfo.imageMipLevel >= vkImage.getMipLevels()) {
            gfx::log::error("Image mip level exceeds image mip levels (image mip levels: {}, image subresource mip level: {})", vkImage.getMipLevels(), copyInfo.imageMipLevel);
            throw std::runtime_error("Image mip level exceeds image mip levels");
        }

        if (copyInfo.imageBaseArrayLayer >= vkImage.getArrayLayers()) {
            gfx::log::error("Image base array layer exceeds image array layers (image array layers: {}, image subresource base array layer: {})", vkImage.getArrayLayers(), copyInfo.imageBaseArrayLayer);
            throw std::runtime_error("Image base array layer exceeds image array layers");
        }

        if (copyInfo.imageExtent == glm::ivec3(-1))
            copyInfo.imageExtent = image->getExtent();

        if (copyInfo.bufferRowLength == 0)
            copyInfo.bufferRowLength = copyInfo.imageExtent.x;
        if (copyInfo.bufferImageHeight == 0)
            copyInfo.bufferImageHeight = copyInfo.imageExtent.y;
        if (copyInfo.bufferRowLength < copyInfo.imageExtent.x || copyInfo.bufferImageHeight < copyInfo.imageExtent.y) {
            gfx::log::error("Buffer row length or image height is too small for the specified image extent (buffer row length: {}, buffer image height: {}, image extent: {}x{})", copyInfo.bufferRowLength, copyInfo.bufferImageHeight, copyInfo.imageExtent.x, copyInfo.imageExtent.y);
            throw std::runtime_error("Buffer row length or image height is too small for the specified image extent");
        }
        if (copyInfo.bufferRowLength * copyInfo.bufferImageHeight * copyInfo.imageExtent.z + copyInfo.bufferOffset > vkBuffer.getSize()) {
            gfx::log::error("Buffer offset + copy size exceeds buffer size (buffer size: {}, buffer offset: {}, copy size: {})", vkBuffer.getSize(), copyInfo.bufferOffset, copyInfo.bufferRowLength * copyInfo.bufferImageHeight * copyInfo.imageExtent.z);
            throw std::runtime_error("Buffer offset + copy size exceeds buffer size");
        }
        if (copyInfo.imageBaseArrayLayer + copyInfo.imageLayerCount > vkImage.getArrayLayers()) {
            gfx::log::error("Image base array layer + layer count exceeds image array layers (image array layers: {}, image subresource base array layer: {}, image subresource layer count: {})", vkImage.getArrayLayers(), copyInfo.imageBaseArrayLayer, copyInfo.imageLayerCount);
            throw std::runtime_error("Image base array layer + layer count exceeds image array layers");
        }
        if (copyInfo.imageMipLevel >= vkImage.getMipLevels()) {
            gfx::log::error("Image mip level exceeds image mip levels (image mip levels: {}, image subresource mip level: {})", vkImage.getMipLevels(), copyInfo.imageMipLevel);
            throw std::runtime_error("Image mip level exceeds image mip levels");
        }

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

    gfx::CommandBuffer & CommandBuffer::CopyImageToBuffer(ResourceRef<const gfx::Image> image, ResourceRef<const gfx::Buffer> buffer, gfx::Copy copyInfo) {
        const auto& vkBuffer = dynamic_cast<const gfx::vk::Buffer&>(*buffer);
        const auto& vkImage = dynamic_cast<const gfx::vk::Image&>(*image);

        if (copyInfo.bufferOffset >= vkBuffer.getSize()) {
            gfx::log::error("Buffer offset exceeds buffer size (buffer size: {}, buffer offset: {})", vkBuffer.getSize(), copyInfo.bufferOffset);
            throw std::runtime_error("Buffer offset exceeds buffer size");
        }

        if (copyInfo.imageMipLevel >= vkImage.getMipLevels()) {
            gfx::log::error("Image mip level exceeds image mip levels (image mip levels: {}, image subresource mip level: {})", vkImage.getMipLevels(), copyInfo.imageMipLevel);
            throw std::runtime_error("Image mip level exceeds image mip levels");
        }

        if (copyInfo.imageBaseArrayLayer >= vkImage.getArrayLayers()) {
            gfx::log::error("Image base array layer exceeds image array layers (image array layers: {}, image subresource base array layer: {})", vkImage.getArrayLayers(), copyInfo.imageBaseArrayLayer);
            throw std::runtime_error("Image base array layer exceeds image array layers");
        }

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

    void CommandBuffer::Submit()
    {
        const auto commandBuffers = std::array { _handle };
        auto submitInfo = ::vk::SubmitInfo()
            .setCommandBuffers(commandBuffers);

        if (_signalSemaphore != nullptr)
            submitInfo.setSignalSemaphores(_signalSemaphore);

        try {
            _queue->submit(submitInfo, _fence);
        } catch (const std::runtime_error& e) {
            std::cerr << e.what() << std::endl;
        }
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
        } else {
            std::cerr << "No pipeline bound when trying to push constants" << std::endl;
        }
        return *this;
    }
}
