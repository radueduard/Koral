//
// Created by radue on 2/20/2026.
//

#pragma once

#include "buffer.h"
#include "imageView.h"
#include "sampler.h"
#include "core/resources/descriptorBinding.h"

namespace gfx::ogl
{
    class Descriptor final : public gfx::Descriptor
    {
    public:
        explicit Descriptor(const Builder& createInfo);

        void Bind(glm::u32 bindingPoint) const override;
    };
}


