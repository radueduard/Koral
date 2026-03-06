//
// Created by radue on 2/28/2026.
//

#pragma once
#include <vulkan/vulkan.hpp>
#include "core/sampler.h"
#include "utils/vk_wrapper.h"

namespace gfx::vk
{
    class Sampler final : public gfx::Sampler, public vk::Wrapper<::vk::Sampler>
    {
    public:
        explicit Sampler(const Builder& builder);
        ~Sampler() override;
    };
}
