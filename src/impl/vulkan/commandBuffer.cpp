//
// Created by radue on 2/27/2026.
//

#include "commandBuffer.h"

#include "device.h"
#include "vulkanContext.h"

namespace gfx::vk
{
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
        _fence = gfx::vk::Context::Device()->createFence(::vk::FenceCreateInfo().setFlags(::vk::FenceCreateFlagBits::eSignaled));
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

    gfx::CommandBuffer& CommandBuffer::BeginRendering(const Framebuffer* framebuffer)
    {
        return *this;
    }

    gfx::CommandBuffer& CommandBuffer::EndRendering()
    {
        return *this;
    }

    gfx::CommandBuffer& CommandBuffer::SetViewport(glm::u32 x, glm::u32 y, glm::u32 width, glm::u32 height)
    {
        return *this;
    }

    gfx::CommandBuffer& CommandBuffer::SetScissor(glm::u32 x, glm::u32 y, glm::u32 width, glm::u32 height)
    {
        return *this;
    }

    gfx::CommandBuffer& CommandBuffer::BindPipeline(const ComputePipeline* pipeline)
    {
        return *this;
    }

    gfx::CommandBuffer& CommandBuffer::BindPipeline(const GraphicsPipeline* pipeline)
    {
        return *this;
    }

    gfx::CommandBuffer& CommandBuffer::BindDescriptorSet(glm::u32 index, const DescriptorSet* set)
    {
        return *this;
    }

    gfx::CommandBuffer& CommandBuffer::Dispatch(glm::u32 groupCountX, glm::u32 groupCountY, glm::u32 groupCountZ)
    {
        return *this;
    }

    gfx::CommandBuffer& CommandBuffer::Draw(glm::u32 vertexCount, glm::u32 instanceCount, glm::u32 firstVertex, glm::u32 firstInstance)
    {
        return *this;
    }

    gfx::CommandBuffer& CommandBuffer::DrawMesh(const Mesh* mesh, glm::u32 instanceCount, glm::u32 baseInstance)
    {
        return *this;
    }

    void CommandBuffer::Submit()
    {

    }

    void CommandBuffer::Reset()
    {
        _handle.reset();
    }
}
