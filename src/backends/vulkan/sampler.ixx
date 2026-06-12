//
// Created by radue on 2/28/2026.
//

module;

#include <vulkan/vulkan.hpp>
#include "vk_wrapper.h"

export module vk.sampler;
import gfx.sampler;

namespace gfx::vk
{
    export class Sampler final : public gfx::Sampler, public vk::Wrapper<::vk::Sampler>
    {
    public:
        explicit Sampler(const Builder& builder);
        ~Sampler() override;
    };
}
