//
// Created by radue on 3/6/2026.
//

#pragma once
#include <shader.h>
#include "vk_wrapper.h"

#include <vulkan/vulkan.hpp>


namespace kor::vk
{
    class Shader final : public kor::Shader, public Wrapper<::vk::ShaderModule>
    {
    public:
        explicit Shader(const Builder& createInfo);
        ~Shader() override;

    protected:
        void OnReload() override;
    };
}
