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

module;

#include <glm/fwd.hpp>
#include "api.h"

export module gfx.graphicsPipeline;

import std;
import gfx.resource;
import gfx.structs;
import gfx.flags;
import gfx.shader;
import gfx.descriptorSetLayout;
import gfx.mesh;

namespace gfx
{
    class Framebuffer;
    class CommandBuffer;

    export struct GFX_API TessellationState
    {
        gfx::ResourceRef<const Shader> controlShader;
        gfx::ResourceRef<const Shader> evalShader;
        glm::u32 patchControlPoints = 3;
    };

    export struct GFX_API ColorBlendState
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

    export class GFX_API GraphicsPipeline
    {
    public:
        struct GFX_API Builder {
            std::optional<gfx::ResourceRef<const Shader>> vertexShader = std::nullopt;
            std::optional<TessellationState> tessellationState = std::nullopt;
            std::optional<gfx::ResourceRef<const Shader>> geometryShader = std::nullopt;
            std::optional<gfx::ResourceRef<const Shader>> fragmentShader = std::nullopt;
            std::optional<gfx::ResourceRef<const Shader>> taskShader = std::nullopt;
            std::optional<gfx::ResourceRef<const Shader>> meshShader = std::nullopt;
            std::optional<gfx::ResourceRef<const Framebuffer>> framebuffer = std::nullopt;
            std::vector<VertexInputAttributeDescription> vertexAttributeDescriptions = {};
            std::vector<VertexInputBindingDescription> vertexBindingDescriptions = {};
            InputAssemblyState inputAssemblyState = {};
            RasterizationState rasterizationState = {};
            MultisampleState multisampleState = {};
            DepthStencilState depthStencilState = {};
            ColorBlendState colorBlendState = {};

            template <gfx::MeshType T>
            Builder& setVertexShader(gfx::ResourceRef<const Shader> shader)
            {
                this->vertexShader = std::cref(shader);
                this->vertexAttributeDescriptions = T::VertexAttributeDescription();
                this->vertexBindingDescriptions = T::VertexBindingDescription();
                return *this;
            }

            Builder& setTessellationState(const TessellationState& tessellationState);
            Builder& setGeometryShader(gfx::ResourceRef<const Shader> geometryShader);
            Builder& setFragmentShader(gfx::ResourceRef<const Shader> fragmentShader);
            Builder& setTaskShader(gfx::ResourceRef<const Shader> taskShader);
            Builder& setMeshShader(gfx::ResourceRef<const Shader> meshShader);
            Builder& setInputAssemblyState(const InputAssemblyState& inputAssemblyState);
            Builder& setRasterizationState(const RasterizationState& rasterizationState);
            Builder& setMultisampleState(const MultisampleState& multisampleState);
            Builder& setDepthStencilState(const DepthStencilState& depthStencilState);
            Builder& setColorBlendState(const ColorBlendState& colorBlendState);
            Builder& setFramebuffer(gfx::ResourceRef<const Framebuffer> framebuffer);
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

        std::optional<gfx::ResourceRef<const Shader>> _vertexShader;
        std::optional<TessellationState> _tessellationState;
        std::optional<gfx::ResourceRef<const Shader>> _geometryShader;
        std::optional<gfx::ResourceRef<const Shader>> _fragmentShader;

        std::optional<gfx::ResourceRef<const Shader>> _taskShader;
        std::optional<gfx::ResourceRef<const Shader>> _meshShader;

        gfx::ResourceRef<const Framebuffer> _framebuffer;

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
