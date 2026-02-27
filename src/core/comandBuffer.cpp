//
// Created by radue on 2/21/2026.
//

#include "comandBuffer.h"

#include "impl/open_gl/commandBuffer.h"
#include "io/window.h"

namespace gfx
{
    std::unique_ptr<CommandBuffer> CommandBuffer::Create(const Flags<Usage> usage)
    {
        switch (Context::Window().getAPI()) {
        case API::OpenGL:
            return std::make_unique<ogl::CommandBuffer>(usage);
        case API::Vulkan:
            throw std::runtime_error("vulkan is not supported");
        default:
            throw std::runtime_error("Unknown API");
        }
    }
} // gfx