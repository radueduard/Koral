//
// Created by radue on 2/27/2026.
//
#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#include "commandBuffer.h"

#include <ranges>

#include "buffer.h"
#include "computePipeline.h"
#include "context.h"
#include "descriptorSet.h"
#include "device.h"
#include "framebuffer.h"
#include "graphicsPipeline.h"
#include "imageView.h"
#include "scheduler.h"
#include "vulkanContext.h"

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
        constexpr auto commandBufferBeginInfo = ::vk::CommandBufferBeginInfo()
            .setFlags(::vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
        _handle.begin(commandBufferBeginInfo);
        return *this;
    }

    void CommandBuffer::End()
    {
        _handle.end();
    }

    gfx::CommandBuffer& CommandBuffer::BeginRendering(const gfx::Framebuffer* framebuffer)
    {
        gfx::CommandBuffer::BeginRendering(framebuffer);
        framebuffer = _state.boundFramebuffer.value();
        if (!framebuffer->IsDefault())
        {
            for (const auto& attachment : framebuffer->getColorAttachments())
            {
                const auto& vkImage = dynamic_cast<const gfx::vk::Image&>(attachment.get().getImage());
                vkImage.TransitionLayout(*this, ::vk::ImageLayout::eColorAttachmentOptimal);
            }
            if (framebuffer->hasDepthStencilAttachment())
            {
                const auto& vkImage = dynamic_cast<const gfx::vk::Image&>(framebuffer->getDepthStencilAttachment().getImage());
                vkImage.TransitionLayout(*this, ::vk::ImageLayout::eDepthStencilAttachmentOptimal);
            }
        }

        std::vector<::vk::RenderingAttachmentInfoKHR> colorAttachmentInfos;
        int i = 0;
        for (auto colorAttachment : framebuffer->getColorAttachments()) {
            colorAttachmentInfos.push_back(::vk::RenderingAttachmentInfoKHR()
                .setImageView(**dynamic_cast<const gfx::vk::ImageView*>(&colorAttachment.get()))
                .setImageLayout(::vk::ImageLayout::eColorAttachmentOptimal)
                .setClearValue(::vk::ClearValue().setColor(::vk::ClearColorValue()
                    .setFloat32({ framebuffer->getClearValues().clearColor[i].r, framebuffer->getClearValues().clearColor[i].g, framebuffer->getClearValues().clearColor[i].b, framebuffer->getClearValues().clearColor[i].a })))
                .setLoadOp(::vk::AttachmentLoadOp::eClear)
                .setStoreOp(::vk::AttachmentStoreOp::eStore));
            i++;
        }

        const auto depthAttachment = framebuffer->hasDepthStencilAttachment() ? std::optional(::vk::RenderingAttachmentInfoKHR()
            .setImageView(**dynamic_cast<const gfx::vk::ImageView*>(&framebuffer->getDepthStencilAttachment()))
            .setImageLayout(::vk::ImageLayout::eDepthStencilAttachmentOptimal)
            .setClearValue(::vk::ClearValue().setDepthStencil({ framebuffer->getClearValues().clearDepth, static_cast<glm::u32>(framebuffer->getClearValues().clearStencil) }))
            .setLoadOp(::vk::AttachmentLoadOp::eClear)
            .setStoreOp(::vk::AttachmentStoreOp::eStore)) : std::nullopt;

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
            .setPStencilAttachment(depthAttachment.has_value()? &depthAttachment.value() : nullptr);

        _handle.beginRenderingKHR(beginRenderingInfo);
        return *this;
    }

    gfx::CommandBuffer& CommandBuffer::EndRendering()
    {
        _handle.endRenderingKHR();
        if (const auto* framebuffer = dynamic_cast<const gfx::vk::Framebuffer*>(_state.boundFramebuffer.value()); !framebuffer->IsDefault())
        {
            for (const auto& attachment : framebuffer->getColorAttachments())
            {
                const auto& vkImage = dynamic_cast<const gfx::vk::Image&>(attachment.get().getImage());
                vkImage.TransitionLayout(*this, ::vk::ImageLayout::eGeneral);
            }
            if (framebuffer->hasDepthStencilAttachment())
            {
                const auto& vkImage = dynamic_cast<const gfx::vk::Image&>(framebuffer->getDepthStencilAttachment().getImage());
                vkImage.TransitionLayout(*this, ::vk::ImageLayout::eGeneral);
            }
        }
        return gfx::CommandBuffer::EndRendering();
    }

    gfx::CommandBuffer& CommandBuffer::SetViewport(glm::u32 x, glm::u32 y, glm::u32 width, glm::u32 height)
    {
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
        const ::vk::Rect2D scissor = ::vk::Rect2D()
            .setOffset({ static_cast<glm::i32>(x), static_cast<glm::i32>(y) })
            .setExtent({ width, height });
        _handle.setScissor(0, scissor);
        return *this;
    }

    gfx::CommandBuffer& CommandBuffer::BindPipeline(const gfx::ComputePipeline* pipeline)
    {
        gfx::CommandBuffer::BindPipeline(pipeline);
        pipeline->Bind(*this);
        return *this;
    }

    gfx::CommandBuffer& CommandBuffer::BindPipeline(const gfx::GraphicsPipeline* pipeline)
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

    gfx::CommandBuffer& CommandBuffer::BindDescriptorSet(const glm::u32 index, const gfx::DescriptorSet* set)
    {
        if (_state.boundComputePipeline.has_value()) {
            const auto& vkPipeline = dynamic_cast<const gfx::vk::ComputePipeline*>(_state.boundComputePipeline.value());
            const auto pipelineLayout = vkPipeline->getPipelineLayout();
            const auto& vkSet = dynamic_cast<const gfx::vk::DescriptorSet*>(set);
            _handle.bindDescriptorSets(
                ::vk::PipelineBindPoint::eCompute,
                pipelineLayout,
                index,
                **vkSet,
                nullptr);
        } else if (_state.boundGraphicsPipeline.has_value()) {
                const auto& vkPipeline = dynamic_cast<const gfx::vk::GraphicsPipeline*>(_state.boundGraphicsPipeline.value());
                const auto pipelineLayout = vkPipeline->getPipelineLayout();
                const auto& vkSet = dynamic_cast<const gfx::vk::DescriptorSet*>(set);
                _handle.bindDescriptorSets(
                    ::vk::PipelineBindPoint::eGraphics,
                    pipelineLayout,
                    index,
                    **vkSet,
                    nullptr);
        } else {
            std::cerr << "No pipeline bound when trying to bind descriptor set" << std::endl;
        }
        return *this;
    }

    gfx::CommandBuffer& CommandBuffer::Dispatch(const glm::u32 groupCountX, const glm::u32 groupCountY, const glm::u32 groupCountZ)
    {
        _handle.dispatch(groupCountX, groupCountY, groupCountZ);
        return *this;
    }

    gfx::CommandBuffer& CommandBuffer::Draw(const glm::u32 vertexCount, const glm::u32 instanceCount, const glm::u32 firstVertex, const glm::u32 firstInstance)
    {
        _handle.draw(vertexCount, instanceCount, firstVertex, firstInstance);
        return *this;
    }

    gfx::CommandBuffer& CommandBuffer::DrawMesh(const Mesh* mesh, glm::u32 instanceCount, glm::u32 baseInstance)
    {
        std::vector<::vk::Buffer> vertexBuffers;
        std::vector<::vk::DeviceSize> offsets;
        for (const auto& buffer : mesh->getVertexBuffers()) {
            const auto& vkBuffer = *dynamic_cast<const gfx::vk::Buffer*>(&buffer.get());
            vertexBuffers.push_back(*vkBuffer);
            offsets.push_back(0);
        }
        _handle.bindVertexBuffers(0, vertexBuffers, offsets);
        if (mesh->hasIndexBuffer())
        {
            const auto& vkBuffer = *dynamic_cast<const gfx::vk::Buffer*>(&mesh->getIndexBuffer().value().get());
            _handle.bindIndexBuffer(*vkBuffer, 0, ::vk::IndexType::eUint32);
        }
        const auto indexCount = mesh->hasIndexBuffer() ? mesh->getIndexCount().value() : mesh->getVertexCount();
        _handle.drawIndexed(indexCount, instanceCount, 0, 0, 0);
        return *this;
    }

    gfx::CommandBuffer& CommandBuffer::Blit(const gfx::Image* srcImage, const gfx::Image* dstImage)
    {
        const auto vkSrcImage = dynamic_cast<const gfx::vk::Image*>(srcImage);
        const gfx::vk::Image* vkDstImage = dstImage != nullptr ? dynamic_cast<const gfx::vk::Image*>(dstImage) : nullptr;
        if (dstImage == nullptr)
            vkDstImage = &dynamic_cast<const gfx::vk::Scheduler&>(gfx::Context::Scheduler()).getSwapChain().getImage();

        vkSrcImage->TransitionLayout(*this, ::vk::ImageLayout::eTransferSrcOptimal);
        vkDstImage->TransitionLayout(*this, ::vk::ImageLayout::eTransferDstOptimal);
        _handle.blitImage(
            **vkSrcImage, ::vk::ImageLayout::eTransferSrcOptimal,
            **vkDstImage, ::vk::ImageLayout::eTransferDstOptimal,
            ::vk::ImageBlit()
                .setSrcSubresource(::vk::ImageSubresourceLayers()
                    .setAspectMask(::vk::ImageAspectFlagBits::eColor)
                    .setMipLevel(0)
                    .setBaseArrayLayer(0)
                    .setLayerCount(1))
                .setSrcOffsets({
                    ::vk::Offset3D{ 0, 0, 0 },
                    ::vk::Offset3D{ static_cast<glm::i32>(vkSrcImage->getExtent().x), static_cast<glm::i32>(vkSrcImage->getExtent().y), 1 }
                })
                .setDstSubresource(::vk::ImageSubresourceLayers()
                    .setAspectMask(::vk::ImageAspectFlagBits::eColor)
                    .setMipLevel(0)
                    .setBaseArrayLayer(0)
                    .setLayerCount(1))
                .setDstOffsets({
                    ::vk::Offset3D{ 0, 0, 0 },
                    ::vk::Offset3D{ static_cast<glm::i32>(vkDstImage->getExtent().x), static_cast<glm::i32>(vkDstImage->getExtent().y), 1 }
                }),
            ::vk::Filter::eNearest);
         vkSrcImage->TransitionLayout(*this, ::vk::ImageLayout::eGeneral);
         vkDstImage->TransitionLayout(*this, ::vk::ImageLayout::eGeneral);
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
}
