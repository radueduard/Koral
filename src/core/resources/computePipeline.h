//
// Created by radue on 2/21/2026.
//

#pragma once
#include <glm/fwd.hpp>

#include "core/resources/buffer.h"
#include "core/resources/shader.h"

namespace gfx
{
    class ComputePipeline {
    public:
        struct CreateInfo {
            std::reference_wrapper<const Shader> computeShader;

            explicit CreateInfo(const Shader& computeShader) : computeShader(computeShader) {}
        };

        static std::unique_ptr<ComputePipeline> Create(const CreateInfo& createInfo);

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


    protected:
        mutable bool _bound = false;
        explicit ComputePipeline(const CreateInfo& createInfo) {};
    };
}

