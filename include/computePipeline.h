//
// Created by radue on 2/21/2026.
//

#pragma once
#include <map>
#include <memory>
#include <optional>
#include <glm/fwd.hpp>

#include "api.h"
#include "descriptorSetLayout.h"
#include "shader.h"

namespace gfx
{
    class Shader;
    class CommandBuffer;

    class GFX_API ComputePipeline {
    public:
        struct GFX_API Builder {
            std::optional<std::reference_wrapper<const Shader>> computeShader;
            Builder& setComputeShader(const Shader& computeShader);
            [[nodiscard]] std::unique_ptr<ComputePipeline> build() const;
        };

        virtual ~ComputePipeline();
        ComputePipeline(const ComputePipeline&) = delete;
        ComputePipeline& operator=(const ComputePipeline&) = delete;

        virtual void Bind(const CommandBuffer& commandBuffer) const;;

        virtual void Unbind() const;

        const DescriptorSetLayout& getSetLayout(glm::u32 index) const;

    protected:
        mutable bool _bound = false;
        explicit ComputePipeline(const Builder& createInfo);

        std::optional<std::reference_wrapper<const Shader>> _shader;
        std::map<glm::u32, std::unique_ptr<DescriptorSetLayout>> _setLayouts;
        std::map<glm::u32, Shader::PushConstant> _pushConstantRanges;
    };
}
