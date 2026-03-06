//
// Created by radue on 2/21/2026.
//

#pragma once
#include <glm/fwd.hpp>

#include "buffer.h"
#include "shader.h"

namespace gfx
{
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

        virtual void Bind() const {
            _bound = true;
        };

        virtual void Unbind() const {
            _bound = false;
        }

        virtual void Dispatch(glm::u32 groupCountX, glm::u32 groupCountY = 1, glm::u32 groupCountZ = 1) const {
            if (!_bound) {
                throw std::runtime_error("Compute pipeline must be bound before dispatching!");
            }
        };
        virtual void DispatchBase(glm::u32 baseGroupX, glm::u32 groupCountX, glm::u32 baseGroupY = 0, glm::u32 groupCountY = 1, glm::u32 baseGroupZ = 0, glm::u32 groupCountZ = 1) const {
            if (!_bound) {
                throw std::runtime_error("Compute pipeline must be bound before dispatching!");
            }
        };
        virtual void DispatchIndirect(const Buffer& indirectBuffer, glm::i64 offset = 0) const {
            if (!_bound) {
                throw std::runtime_error("Compute pipeline must be bound before dispatching!");
            }
        };

        const gfx::DescriptorSetLayout& getSetLayout(glm::u32 index) const;

    protected:
        mutable bool _bound = false;
        explicit ComputePipeline(const Builder& createInfo);

        std::optional<std::reference_wrapper<const Shader>> _shader;
        std::map<glm::u32, std::unique_ptr<DescriptorSetLayout>> _setLayouts;
    };
}

