//
// Created by radue on 2/21/2026.
//

#pragma once
#include <GL/glew.h>
#include <computePipeline.h>


namespace gfx::ogl
{
    class ComputePipeline final : public gfx::ComputePipeline {
    public:
        explicit ComputePipeline(const Builder& createInfo);
        ~ComputePipeline() override;
        void Bind(const gfx::CommandBuffer& commandBuffer) const override;
        void Unbind() const override;

        const std::map<std::pair<glm::u32, glm::u32>, glm::u32>& getSetAndBindingToBindingPointMap() const {
            return _setAndBindingToBindingPoint;
        }

    private:
        GLuint _id = 0;
        std::map<std::pair<glm::u32, glm::u32>, glm::u32> _setAndBindingToBindingPoint;
    };
}