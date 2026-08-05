//
// Created by radue on 2/21/2026.
//

#pragma once
#include <map>
#include <memory>
#include <optional>
#include <vector>
#include <tuple>
#include <cstring>
#include <stdexcept>
#include <glm/fwd.hpp>

#include "api.h"
#include <source_location>

#include "builder.h"
#include "descriptorSetLayout.h"
#include "pipeline.h"
#include "resource.h"
#include "shader.h"

namespace kor
{
    class Shader;
    class CommandBuffer;

    /**
     * @brief A compiled compute pipeline: one shader, run over a grid of workgroups.
     *
     * The simplest pipeline type — no fixed-function state, just the shader and whatever
     * specialization constants are baked into it.
     *
     * @code
     * kor::ComputePipeline::Builder builder;
     * auto cull = builder.setComputeShader(cullShader).build();
     *
     * commandBuffer.BindComputePipeline(cull).Dispatch(groupsX, 1, 1);
     * @endcode
     *
     * @see Pipeline for the descriptor reflection and hot reload it inherits
     */
    class KORAL_API ComputePipeline : public Pipeline {
    public:
        /** @brief Collects the shader a compute pipeline is compiled from. */
        struct KORAL_API Builder : ::Builder {
            // Repairable: its inputs are a source file (shaders) or lifetime-tracked shader refs
            // (pipelines), so a failure here can be fixed at runtime and retried. See Builder::Recoverable.
            static constexpr bool Recoverable = true;

            std::optional<ResourceRef<const Shader>> computeShader;    ///< The shader to run.

            /** @brief Sets the compute shader. Required. */
            Builder& setComputeShader(ResourceRef<const Shader> computeShader);

            /**
             * @brief Bakes a specialization constant into the shader.
             * @tparam T The constant's type; must be trivially copyable.
             * @param id The constant id the shader declares.
             * @param value The value compiled in.
             *
             * Commonly used to fix a workgroup size or a feature switch at build time, so the
             * compiler can fold branches and unroll loops it otherwise could not.
             *
             * @throws std::runtime_error if the accumulated constants exceed the internal buffer.
             */
            template<typename T> requires std::is_trivially_copyable_v<T>
            Builder& setSpecializationConstant(glm::u32 id, T value) {
                const glm::u32 valueSize = sizeof(T);
                if (currentSpecConstantSize + valueSize > specConstantsData.size()) {
                    throw std::runtime_error("Exceeded maximum specialization constant data size");
                }
                specConstantsMetadata.emplace_back(id, currentSpecConstantSize, valueSize);
                std::memcpy(specConstantsData.data() + currentSpecConstantSize, &value, valueSize);
                currentSpecConstantSize += valueSize;
                return *this;
            }

            /** @brief One build attempt. Internal: prefer build(). */
            [[nodiscard]] Result<std::unique_ptr<ComputePipeline>> create() const;

            /**
             * @brief Compiles the pipeline.
             * @return It as a Resource; poisoned rather than thrown when the shader fails to
             *         compile, and repaired automatically when it is fixed.
             */
            [[nodiscard]] kor::Resource<ComputePipeline> build(std::source_location where = std::source_location::current()) const;

            std::vector<std::tuple<glm::u32, glm::u32, glm::u32>> specConstantsMetadata {};
            std::vector<std::byte> specConstantsData = std::vector<std::byte>(64, static_cast<std::byte>(0));

        private:
            glm::u32 currentSpecConstantSize = 0;
        };

        ~ComputePipeline() override;

        /** @brief Binds the pipeline. Called by the backend; a scene uses CommandBuffer::BindComputePipeline. */
        void Bind(const CommandBuffer& commandBuffer) const override;

        /** @brief Unbinds it, where the backend has such a notion. */
        void Unbind() const override;

    protected:
        explicit ComputePipeline(const Builder& createInfo);

        VoidResult Validate() override;

        std::optional<ResourceRef<const Shader>> _shader;

        std::vector<std::tuple<glm::u32, glm::u32, glm::u32>> _specConstantsMetadata;
        std::vector<std::byte> _specConstantsData;
    };
}
