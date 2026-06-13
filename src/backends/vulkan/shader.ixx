//
// Created by radue on 3/6/2026.
//

module;

#include "vk_wrapper.h"
#include <vulkan/vulkan.hpp>

export module gfx:vk_shader;
import :vk_types;

import :shader;

namespace gfx::vk
{
    export class Shader final : public gfx::Shader, public Wrapper<::vk::ShaderModule>
    {
    public:
        explicit Shader(const Builder& createInfo);
        ~Shader() override;
    };
}
