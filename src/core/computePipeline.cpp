//
// Created by radue on 2/21/2026.
//
#include "computePipeline.h"
#include "impl/open_gl/computePipeline.h"

#include "io/window.h"

namespace gfx
{
    std::unique_ptr<ComputePipeline> ComputePipeline::Builder::build() const
    {
        switch (Context::Window().getAPI()) {
        case API::OpenGL:
            return std::make_unique<ogl::ComputePipeline>(*this);
        case API::Vulkan:
            throw std::runtime_error("Vulkan is not supported yet!");
        default:
            throw std::runtime_error("Unknown graphics API!");
        }
    }
}
