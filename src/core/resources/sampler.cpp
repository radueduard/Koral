//
// Created by radue on 2/20/2026.
//

#include "sampler.h"
#include "impl/open_gl/resources/sampler.h"

#include "context.h"
#include "io/window.h"

namespace gfx
{
    std::unique_ptr<Sampler> Sampler::Create(const CreateInfo& createInfo)
    {
        switch (Context::Window().getAPI()) {
        case API::OpenGL:
            return std::make_unique<ogl::Sampler>(createInfo);
        case API::Vulkan:
            throw std::runtime_error("Vulkan is not supported yet!");
        default:
            throw std::runtime_error("Unknown graphics API!");
        }
    }
}
