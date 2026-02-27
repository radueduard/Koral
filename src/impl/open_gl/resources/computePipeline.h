//
// Created by radue on 2/21/2026.
//

#pragma once
#include <GL/glew.h>

#include "core/resources/computePipeline.h"

namespace gfx::ogl
{
    class ComputePipeline final : public gfx::ComputePipeline {
    public:
        explicit ComputePipeline(const Builder& createInfo);
        ~ComputePipeline() override;
        void Bind() const override;
        void Unbind() const override;
        void Dispatch(glm::u32 groupCountX, glm::u32 groupCountY, glm::u32 groupCountZ) const override;
        void DispatchIndirect(const gfx::Buffer& indirectBuffer, glm::i64 offset) const override;

    private:
        GLuint _id = 0;
    };
}