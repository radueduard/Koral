//
// Created by radue on 3/6/2026.
//

#include "shader.h"

#include "device.h"
#include "vulkanContext.h"

namespace gfx::vk
{
    Shader::Shader(const Builder& createInfo): gfx::Shader(createInfo)
    {
        const auto shaderModuleCreateInfo = ::vk::ShaderModuleCreateInfo()
            .setCode(_spirvCode);
        _handle = Context::Device()->createShaderModule(shaderModuleCreateInfo);
    }

    Shader::~Shader()
    {
        if (_handle) {
            Context::Device()->destroyShaderModule(_handle);
        }
    }

    void Shader::OnReload()
    {
        gfx::Shader::OnReload();
        if (_handle) {
            Context::Device()->destroyShaderModule(_handle);
        }
        const auto shaderModuleCreateInfo = ::vk::ShaderModuleCreateInfo()
            .setCode(_spirvCode);
        _handle = Context::Device()->createShaderModule(shaderModuleCreateInfo);
    }
}
