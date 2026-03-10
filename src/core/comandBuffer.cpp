//
// Created by radue on 2/21/2026.
//

#include "comandBuffer.h"

#include "impl/open_gl/commandBuffer.h"
#include "impl/vulkan/commandBuffer.h"
#include "impl/vulkan/device.h"
#include "impl/vulkan/vulkanContext.h"
#include "io/window.h"

namespace gfx
{
    ::vk::QueueFlags getQueueFlagsFromUsage(const Flags<CommandBuffer::Usage> usage)
    {
        ::vk::QueueFlags queueFlags;
        if (usage & CommandBuffer::Usage::eCompute)
            queueFlags |= ::vk::QueueFlagBits::eCompute;
        if (usage & CommandBuffer::Usage::eGraphics)
            queueFlags |= ::vk::QueueFlagBits::eGraphics;
        if (usage & CommandBuffer::Usage::eTransfer)
            queueFlags |= ::vk::QueueFlagBits::eTransfer;
        return queueFlags;
    }

    CommandBuffer& CommandBuffer::BindPipeline(const ComputePipeline* pipeline)
    {
        _state.boundComputePipeline = pipeline;
        _state.boundGraphicsPipeline = std::nullopt;
        return *this;
    }

    CommandBuffer& CommandBuffer::BindPipeline(const GraphicsPipeline* pipeline)
    {
        _state.boundGraphicsPipeline = pipeline;
        _state.boundComputePipeline = std::nullopt;
        return *this;
    }

    std::unique_ptr<CommandBuffer> CommandBuffer::Create(const Flags<Usage> usage)
    {
        switch (Context::Window().getAPI()) {
        case API::eOpenGL:
            return std::make_unique<ogl::CommandBuffer>(usage);
        case API::eVulkan:
            {
                const auto& queue = vk::Context::Device().requestQueue(getQueueFlagsFromUsage(usage));
                return vk::Context::Device().requestCommandBuffer(queue, vk::Context::ThreadId());
            }
        default:
            throw std::runtime_error("Unknown API");
        }
    }
}
