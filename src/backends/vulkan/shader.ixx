//
// Created by radue on 3/6/2026.
//

module;

#include "vk_wrapper.h"
#include <vulkan/vulkan.hpp>

export module vk.shader;
import gfx.shader;

namespace gfx::vk
{
    export class Shader final : public gfx::Shader, public Wrapper<::vk::ShaderModule>
    {
    public:
        explicit Shader(const Builder& createInfo);
        ~Shader() override;
    };
}
