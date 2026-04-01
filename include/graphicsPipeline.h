//
// Created by radue on 2/22/2026.
//

#pragma once
#include <optional>
#include <functional>
#include <glm/fwd.hpp>

#include <flags.h>
#include <map>
#include <vector>

#include "descriptorSetLayout.h"
#include "mesh.h"
#include "api.h"
#include "shader.h"

namespace gfx
{
    class Framebuffer;
    class Shader;
    class CommandBuffer;

    struct GFX_API TessellationState
    {
        std::reference_wrapper<const Shader> controlShader;
        std::reference_wrapper<const Shader> evalShader;
        glm::u32 patchControlPoints = 3;
    };

    struct GFX_API ColorBlendState
    {
        struct AttachmentState
        {
            bool blendEnable = false;
            BlendFactor srcColorBlendFactor = BlendFactor::eOne;
            BlendFactor dstColorBlendFactor = BlendFactor::eZero;
            BlendOp colorBlendOp = BlendOp::eAdd;
            BlendFactor srcAlphaBlendFactor = BlendFactor::eOne;
            BlendFactor dstAlphaBlendFactor = BlendFactor::eZero;
            BlendOp alphaBlendOp = BlendOp::eAdd;
            Flags<ColorComponent> colorWriteMask = Flags(ColorComponent::eR) | ColorComponent::eG | ColorComponent::eB | ColorComponent::eA;
        };

        bool enableLogicOp = false;
        LogicOp logicOp = LogicOp::eCopy;
        std::vector<AttachmentState> attachments = {};
        float blendConstants[4] = { 0.f, 0.f, 0.f, 0.f };
    };

    class GFX_API GraphicsPipeline
    {
    public:
        struct GFX_API Builder {
            std::optional<std::reference_wrapper<const Shader>> vertexShader = std::nullopt;
            std::optional<TessellationState> tessellationState = std::nullopt;
            std::optional<std::reference_wrapper<const Shader>> geometryShader = std::nullopt;
            std::optional<std::reference_wrapper<const Shader>> fragmentShader = std::nullopt;

            std::optional<std::reference_wrapper<const Shader>> taskShader = std::nullopt;
            std::optional<std::reference_wrapper<const Shader>> meshShader = std::nullopt;

            std::optional<std::reference_wrapper<const Framebuffer>> framebuffer = std::nullopt;

            std::vector<VertexInputAttributeDescription> vertexAttributeDescriptions = {};
            std::vector<VertexInputBindingDescription> vertexBindingDescriptions = {};

            InputAssemblyState inputAssemblyState = {};
            RasterizationState rasterizationState = {};
            MultisampleState multisampleState = {};
            DepthStencilState depthStencilState = {};
            ColorBlendState colorBlendState = {};

            // implicit default (registry -> NullMesh fallback)
            Builder& setVertexShader(const Shader& shader);

            // explicit mesh override
            template <gfx::MeshType T>
            Builder& setVertexShader(const Shader& shader)
            {
                this->vertexShader = std::cref(shader);
                this->vertexAttributeDescriptions = T::VertexAttributeDescription();
                this->vertexBindingDescriptions = T::VertexBindingDescription();
                return *this;
            }

            Builder& setTessellationState(const TessellationState& tessellationState);
            Builder& setGeometryShader(const Shader& geometryShader);
            Builder& setFragmentShader(const Shader& fragmentShader);

            Builder& setTaskShader(const Shader& taskShader);
            Builder& setMeshShader(const Shader& meshShader);

            Builder& setInputAssemblyState(const InputAssemblyState& inputAssemblyState);
            Builder& setRasterizationState(const RasterizationState& rasterizationState);
            Builder& setMultisampleState(const MultisampleState& multisampleState);
            Builder& setDepthStencilState(const DepthStencilState& depthStencilState);
            Builder& setColorBlendState(const ColorBlendState& colorBlendState);
            Builder& setFramebuffer(const Framebuffer& framebuffer);

            [[nodiscard]] std::unique_ptr<GraphicsPipeline> build() const;
        };

        virtual ~GraphicsPipeline();
        GraphicsPipeline(const GraphicsPipeline&) = delete;
        GraphicsPipeline& operator=(const GraphicsPipeline&) = delete;


        virtual void Bind(const gfx::CommandBuffer& commandBuffer) const = 0;
        virtual void Unbind() const = 0;

        [[nodiscard]] const gfx::DescriptorSetLayout& getSetLayout(glm::u32 index) const;
        [[nodiscard]] const Shader::PushConstant& getPushConstantRange(glm::u32 offset) const;

        [[nodiscard]] const std::optional<std::vector<VertexInputBindingDescription>>& getVertexBindingDescriptions() const { return _vertexBindingDescriptions; }
        [[nodiscard]] const std::optional<std::vector<VertexInputAttributeDescription>>& getVertexAttributeDescriptions() const { return _vertexAttributeDescriptions; }

        [[nodiscard]] const InputAssemblyState& getInputAssemblyState() const { return _inputAssemblyState; }
        [[nodiscard]] const RasterizationState& getRasterizationState() const { return _rasterizationState; }
        [[nodiscard]] const MultisampleState& getMultisampleState() const { return _multisampleState; }
        [[nodiscard]] const DepthStencilState& getDepthStencilState() const { return _depthStencilState; }
        [[nodiscard]] const ColorBlendState& getColorBlendState() const { return _colorBlendState; }

    protected:
        explicit GraphicsPipeline(const Builder& createInfo);

        std::optional<std::reference_wrapper<const Shader>> _vertexShader;
        std::optional<TessellationState> _tessellationState;
        std::optional<std::reference_wrapper<const Shader>> _geometryShader;
        std::optional<std::reference_wrapper<const Shader>> _fragmentShader;

        std::optional<std::reference_wrapper<const Shader>> _taskShader;
        std::optional<std::reference_wrapper<const Shader>> _meshShader;

        std::reference_wrapper<const Framebuffer> _framebuffer;

        InputAssemblyState _inputAssemblyState;
        RasterizationState _rasterizationState;
        MultisampleState _multisampleState;
        DepthStencilState _depthStencilState;
        ColorBlendState _colorBlendState;

        std::optional<std::vector<VertexInputAttributeDescription>> _vertexAttributeDescriptions;
        std::optional<std::vector<VertexInputBindingDescription>> _vertexBindingDescriptions;

        std::map<glm::u32, std::unique_ptr<DescriptorSetLayout>> _setLayouts {};
        std::map<glm::u32, Shader::PushConstant> _pushConstantRanges;
    };
}
