//
// Created by radue on 2/21/2026.
//

#include <commandBuffer.h>
#include <framebuffer.h>
#include <surface.h>

#include "../backends/open_gl/commandBuffer.h"
#include "../backends/vulkan/commandBuffer.h"
#include "../backends/vulkan/device.h"
#include "../backends/vulkan/vulkanContext.h"
#include "../../include/window.h"

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

    CommandBuffer& CommandBuffer::BeginRendering(const Framebuffer* framebuffer)
    {
        if (framebuffer == nullptr) framebuffer = &Context::DefaultFramebuffer();
        _state.boundFramebuffer = framebuffer;
        _state.boundComputePipeline = std::nullopt;
        _state.boundGraphicsPipeline = std::nullopt;
        _state.viewportSet = false;
        _state.scissorSet = false;
        return *this;
    }

    CommandBuffer& CommandBuffer::EndRendering()
    {
        _state.boundFramebuffer = std::nullopt;
        _state.boundComputePipeline = std::nullopt;
        _state.boundGraphicsPipeline = std::nullopt;
        return *this;
    }

    CommandBuffer& CommandBuffer::SetViewport(glm::u32 x, glm::u32 y, glm::u32 width, glm::u32 height)
    {
        if (!_state.boundGraphicsPipeline.has_value())
            throw std::runtime_error("You can't set the viewport without a graphics pipeline bound!");
        _state.viewportSet = true;
        return *this;
    }

    CommandBuffer& CommandBuffer::SetScissor(glm::u32 x, glm::u32 y, glm::u32 width, glm::u32 height)
    {
        if (!_state.boundGraphicsPipeline.has_value())
            throw std::runtime_error("You can't set the scissor without a graphics pipeline bound!");
        _state.scissorSet = true;
        return *this;
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

    CommandBuffer& CommandBuffer::Draw(glm::u32 vertexCount, glm::u32 instanceCount, glm::u32 firstVertex,
        glm::u32 firstInstance)
    {
        if (!_state.boundGraphicsPipeline.has_value())
            throw std::runtime_error("You can't draw without a graphics pipeline bound!");
        if (!_state.viewportSet) {
            this->SetViewport(0, 0, Context::Window().getExtent().x, Context::Window().getExtent().y);
        }
        if (!_state.scissorSet) {
            this->SetScissor(0, 0, Context::Window().getExtent().x, Context::Window().getExtent().y);
        }
        return *this;
    }

    CommandBuffer& CommandBuffer::DrawMesh(const Mesh* mesh, glm::u32 instanceCount, glm::u32 baseInstance)
    {
        if (!_state.boundGraphicsPipeline.has_value())
            throw std::runtime_error("You can't draw a mesh without a graphics pipeline bound!");
        if (!_state.viewportSet) {
            this->SetViewport(0, 0, Context::Window().getExtent().x, Context::Window().getExtent().y);
        }
        if (!_state.scissorSet) {
            this->SetScissor(0, 0, Context::Window().getExtent().x, Context::Window().getExtent().y);
        }
        return *this;
    }

    CommandBuffer & CommandBuffer::DrawSubMesh(const Mesh *mesh, glm::u32 baseIndex, glm::u32 indexCount) {
        if (!_state.boundGraphicsPipeline.has_value())
            throw std::runtime_error("You can't draw a mesh without a graphics pipeline bound!");
        if (!_state.viewportSet) {
            this->SetViewport(0, 0, Context::Window().getExtent().x, Context::Window().getExtent().y);
        }
        if (!_state.scissorSet) {
            this->SetScissor(0, 0, Context::Window().getExtent().x, Context::Window().getExtent().y);
        }
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
                return vk::Context::Device().requestCommandBuffer(queue, std::hash<std::thread::id>{}(std::this_thread::get_id()));
            }
        default:
            throw std::runtime_error("Unknown API");
        }
    }
}
