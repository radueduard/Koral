#pragma once

#include <vector>

#include <shader.h>

#include "device.h"
#include "shader.h"
#include "vulkanContext.h"

namespace kor::vk
{
    /**
     * @brief The shader modules one pipeline is created from: each shader's own, or — when the pipeline
     *        numbers its descriptors itself (Pipeline::Bindings) — one made from its SPIR-V renumbered.
     *
     * Lives for one pipeline creation. A module is needed only until the pipeline exists, so those made
     * here are destroyed with this, and the shader's own one is never touched.
     */
    class ShaderModules {
    public:
        explicit ShaderModules(const kor::Shader::BindingAssignment& bindings) : _bindings(bindings) {}
        ShaderModules(const ShaderModules&) = delete;
        ShaderModules& operator=(const ShaderModules&) = delete;
        ~ShaderModules()
        {
            for (const auto module : _made) Context::Device()->destroyShaderModule(module);
        }

        ::vk::ShaderModule For(const kor::Shader& shader)
        {
            if (_bindings.empty()) return *dynamic_cast<const Shader&>(shader);
            const auto code = shader.SpirvWith(_bindings);
            const auto module = Context::Device()->createShaderModule(::vk::ShaderModuleCreateInfo().setCode(code));
            _made.push_back(module);
            return module;
        }

    private:
        const kor::Shader::BindingAssignment& _bindings;
        std::vector<::vk::ShaderModule> _made;
    };
}
