//
// Created by radue on 2/21/2026.
//

#pragma once
#include <optional>
#include <glm/fwd.hpp>

#include "buffer.h"
#include "descriptorSet.h"
#include "shader.h"

namespace gfx
{
    class CommandBuffer;
    class DescriptorSetLayout;

    class ComputePipeline {
    public:
        struct Builder {
            std::optional<std::reference_wrapper<const Shader>> computeShader;

            Builder& setComputeShader(const Shader& computeShader)
            {
                this->computeShader = computeShader;
                return *this;
            }


            [[nodiscard]] std::unique_ptr<ComputePipeline> build() const;
        };

        virtual ~ComputePipeline() = default;

        virtual void Bind(const gfx::CommandBuffer& commandBuffer) const {
            _bound = true;
        };

        virtual void Unbind() const {
            _bound = false;
        }

        const gfx::DescriptorSetLayout& getSetLayout(glm::u32 index) const;

    protected:
        mutable bool _bound = false;
        explicit ComputePipeline(const Builder& createInfo);

        std::optional<std::reference_wrapper<const Shader>> _shader;
        std::map<glm::u32, std::unique_ptr<DescriptorSetLayout>> _setLayouts;
    };
}

