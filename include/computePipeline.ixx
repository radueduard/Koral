//
// Created by radue on 2/21/2026.
//

module;

#include <glm/fwd.hpp>
#include "api.h"

export module gfx.computePipeline;

import std;
import gfx.resource;
import gfx.descriptorSetLayout;

namespace gfx
{
    class Shader;
    class CommandBuffer;

    export class GFX_API ComputePipeline {
    public:
        struct GFX_API Builder {
            std::optional<gfx::ResourceRef<const Shader>> computeShader;
            Builder& setComputeShader(gfx::ResourceRef<const Shader> computeShader);

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
            [[nodiscard]] Resource<ComputePipeline> build() const;

            std::vector<std::tuple<glm::u32, glm::u32, glm::u32>> specConstantsMetadata {};
            std::vector<std::byte> specConstantsData = std::vector(64, static_cast<std::byte>(0));

        private:
            glm::u32 currentSpecConstantSize = 0;
        };

        virtual ~ComputePipeline();
        ComputePipeline(const ComputePipeline&) = delete;
        ComputePipeline& operator=(const ComputePipeline&) = delete;

        virtual void Bind(const gfx::CommandBuffer& commandBuffer) const;;

        virtual void Unbind() const;

        const DescriptorSetLayout& getSetLayout(glm::u32 index) const;

    protected:
        mutable bool _bound = false;
        explicit ComputePipeline(const Builder& createInfo);

        std::optional<gfx::ResourceRef<const Shader>> _shader;
        std::map<glm::u32, std::unique_ptr<DescriptorSetLayout>> _setLayouts;
        std::map<glm::u32, gfx::Shader::PushConstant> _pushConstantRanges;
    };
}
