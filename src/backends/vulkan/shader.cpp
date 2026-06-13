//
// Created by radue on 3/6/2026.
//

module;

#include <vulkan/vulkan.hpp>

module gfx;
import :vk_shader;

import :vk_context;
import :vk_device;

namespace gfx::vk
{
    Shader::Shader(const Builder& createInfo): gfx::Shader(createInfo)
    {
        _spirvCode = CompileToSPIRV(_path, _stage);
        _memoryLayout = fetchMemoryLayout(_spirvCode);

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
}
