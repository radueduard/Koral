//
// Created by radue on 3/6/2026.
//

#pragma once
#include "core/shader.h"
#include "utils/vk_wrapper.h"

#include <vulkan/vulkan.hpp>


namespace gfx::vk
{
    class Shader final : public gfx::Shader, public Wrapper<::vk::ShaderModule>
    {
    public:
        explicit Shader(const Builder& createInfo);
        ~Shader() override;
    };
}
