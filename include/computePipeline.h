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
#include "builder.h"
#include "descriptorSetLayout.h"
#include "pipeline.h"
#include "resource.h"
#include "shader.h"

namespace gfx
{
    class Shader;
    class CommandBuffer;

    class GFX_API ComputePipeline : public Pipeline {
    public:
        struct GFX_API Builder : ::Builder {
            // Repairable: its inputs are a source file (shaders) or lifetime-tracked shader refs
            // (pipelines), so a failure here can be fixed at runtime and retried. See Builder::Recoverable.
            static constexpr bool Recoverable = true;

            std::optional<ResourceRef<const Shader>> computeShader;
            Builder& setComputeShader(ResourceRef<const Shader> computeShader);

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
            [[nodiscard]] gfx::Resource<ComputePipeline> build() const;

            std::vector<std::tuple<glm::u32, glm::u32, glm::u32>> specConstantsMetadata {};
            std::vector<std::byte> specConstantsData = std::vector<std::byte>(64, static_cast<std::byte>(0));

        private:
            glm::u32 currentSpecConstantSize = 0;
        };

        ~ComputePipeline() override;

        void Bind(const CommandBuffer& commandBuffer) const override;
        void Unbind() const override;

    protected:
        explicit ComputePipeline(const Builder& createInfo);

        VoidResult Validate() override;

        std::optional<ResourceRef<const Shader>> _shader;

        std::vector<std::tuple<glm::u32, glm::u32, glm::u32>> _specConstantsMetadata;
        std::vector<std::byte> _specConstantsData;
    };
}
