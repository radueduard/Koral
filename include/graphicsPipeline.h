//
// Created by radue on 2/22/2026.
//

/**
 * @file graphicsPipeline.h
 * @brief Graphics pipeline abstraction and builder configuration types.
 *
 * Defines the high-level graphics pipeline interface used by the runtime, along
 * with immutable state descriptions (rasterization, blending, depth/stencil, etc.)
 * and shader-stage selection through `GraphicsPipeline::Builder`.
 */

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
        struct GFX_API AttachmentState
        {
            bool blendEnable = false;
            BlendFactor srcColorBlendFactor = BlendFactor::eOne;
            BlendFactor dstColorBlendFactor = BlendFactor::eZero;
            BlendOp colorBlendOp = BlendOp::eAdd;
            BlendFactor srcAlphaBlendFactor = BlendFactor::eOne;
            BlendFactor dstAlphaBlendFactor = BlendFactor::eZero;
            BlendOp alphaBlendOp = BlendOp::eAdd;
            Flags<ColorComponent> colorWriteMask =
                Flags(ColorComponent::eR) | ColorComponent::eG | ColorComponent::eB | ColorComponent::eA;
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
            std::optional<gfx::ResourceRef<Framebuffer>> framebuffer = std::nullopt;
            std::vector<VertexInputAttributeDescription> vertexAttributeDescriptions = {};
            std::vector<VertexInputBindingDescription> vertexBindingDescriptions = {};
            InputAssemblyState inputAssemblyState = {};
            RasterizationState rasterizationState = {};
            MultisampleState multisampleState = {};
            DepthStencilState depthStencilState = {};
            ColorBlendState colorBlendState = {};

            Builder& setVertexShader(ResourceRef<const Shader> shader);

            template <gfx::MeshType T>
            Builder& setVertexShader(ResourceRef<const Shader> shader)
            {
                this->vertexShader = std::cref(*shader);
                this->vertexAttributeDescriptions = T::VertexAttributeDescription();
                this->vertexBindingDescriptions = T::VertexBindingDescription();
                return *this;
            }

            Builder& setTessellationState(const TessellationState& tessellationState);
            Builder& setGeometryShader(ResourceRef<const Shader> geometryShader);
            Builder& setFragmentShader(ResourceRef<const Shader> fragmentShader);
            Builder& setTaskShader(ResourceRef<const Shader> taskShader);
            Builder& setMeshShader(ResourceRef<const Shader> meshShader);
            Builder& setInputAssemblyState(const InputAssemblyState& inputAssemblyState);
            Builder& setRasterizationState(const RasterizationState& rasterizationState);
            Builder& setMultisampleState(const MultisampleState& multisampleState);
            Builder& setDepthStencilState(const DepthStencilState& depthStencilState);
            Builder& setColorBlendState(const ColorBlendState& colorBlendState);
            Builder& setFramebuffer(gfx::ResourceRef<Framebuffer> framebuffer);
            [[nodiscard]] gfx::Resource<GraphicsPipeline> build() const;
        };

        /** @brief Virtual destructor for polymorphic ownership. */
        virtual ~GraphicsPipeline();

        GraphicsPipeline(const GraphicsPipeline&) = delete;
        GraphicsPipeline& operator=(const GraphicsPipeline&) = delete;

        /**
         * @brief Bind this pipeline on a command buffer.
         * @param commandBuffer Command buffer receiving bind commands.
         */
        virtual void Bind(const gfx::CommandBuffer& commandBuffer) const = 0;

        /** @brief Unbind this pipeline, if supported by the backend. */
        virtual void Unbind() const = 0;

        /**
         * @brief Get descriptor set layout for a set index.
         * @param index Descriptor set number.
         * @return Descriptor set layout associated with @p index.
         */
        [[nodiscard]] const gfx::DescriptorSetLayout& getSetLayout(glm::u32 index) const;

        /**
         * @brief Get push-constant range by byte offset.
         * @param offset Byte offset into declared push constant ranges.
         * @return Push-constant range covering @p offset.
         */
        [[nodiscard]] const Shader::PushConstant& getPushConstantRange(glm::u32 offset) const;

        /** @brief Vertex input binding descriptions, if available. */
        [[nodiscard]] const std::optional<std::vector<VertexInputBindingDescription>>& getVertexBindingDescriptions() const { return _vertexBindingDescriptions; }

        /** @brief Vertex input attribute descriptions, if available. */
        [[nodiscard]] const std::optional<std::vector<VertexInputAttributeDescription>>& getVertexAttributeDescriptions() const { return _vertexAttributeDescriptions; }

        /** @brief Configured input assembly state. */
        [[nodiscard]] const InputAssemblyState& getInputAssemblyState() const { return _inputAssemblyState; }

        /** @brief Configured rasterization state. */
        [[nodiscard]] const RasterizationState& getRasterizationState() const { return _rasterizationState; }

        /** @brief Configured multisample state. */
        [[nodiscard]] const MultisampleState& getMultisampleState() const { return _multisampleState; }

        /** @brief Configured depth/stencil state. */
        [[nodiscard]] const DepthStencilState& getDepthStencilState() const { return _depthStencilState; }

        /** @brief Configured color blend state. */
        [[nodiscard]] const ColorBlendState& getColorBlendState() const { return _colorBlendState; }

    protected:
        /**
         * @brief Construct from builder configuration.
         * @param createInfo Builder snapshot used to initialize immutable state.
         */
        explicit GraphicsPipeline(const Builder& createInfo);

        std::optional<std::reference_wrapper<const Shader>> _vertexShader;
        std::optional<TessellationState> _tessellationState;
        std::optional<std::reference_wrapper<const Shader>> _geometryShader;
        std::optional<std::reference_wrapper<const Shader>> _fragmentShader;

        std::optional<std::reference_wrapper<const Shader>> _taskShader;
        std::optional<std::reference_wrapper<const Shader>> _meshShader;

        gfx::ResourceRef<Framebuffer> _framebuffer;

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