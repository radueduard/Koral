//
// Created by radue on 2/21/2026.
//

#include "shader.h"
#include "impl/open_gl/shader.h"

#include "context.h"
#include "io/window.h"

namespace gfx {
    std::unique_ptr<Shader> Shader::Builder::build() const
    {
        switch (Context::Window().getAPI()) {
        case API::OpenGL:
            return std::make_unique<ogl::Shader>(*this);
        case API::Vulkan:
            throw std::runtime_error("Vulkan is not supported yet!");
        default:
            throw std::runtime_error("Unknown graphics API!");
        }
    }

    Shader::Shader(const Builder& createInfo) :
        _stage(createInfo.stage),
        _lang(createInfo.lang),
        _path(createInfo.path)
    {
        if (_path.empty()) {
            throw std::runtime_error("Shader path cannot be empty!");
        }
    }
}
