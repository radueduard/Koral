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

        // Push-constant UBO backing (see CommandBuffer::PushConstants). Zero-sized when
        // the pipeline declares no push constants.
        [[nodiscard]] bool hasPushConstants() const { return _pushConstantSize > 0; }
        [[nodiscard]] GLuint getPushConstantUBO() const { return _pushConstantUBO; }
        [[nodiscard]] glm::u32 getPushConstantBindingPoint() const { return _pushConstantBindingPoint; }
        [[nodiscard]] glm::u32 getPushConstantSize() const { return _pushConstantSize; }

        [[nodiscard]] GLuint getProgram() const { return _id; }

    protected:
        void Setup() override;
        void Teardown() override;

    private:
        GLuint _id = 0;
        std::map<std::pair<glm::u32, glm::u32>, glm::u32> _setAndBindingToBindingPoint;

        GLuint _pushConstantUBO = 0;
        glm::u32 _pushConstantBindingPoint = 0;
        glm::u32 _pushConstantSize = 0;
    };
}