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

    /**
     * @brief Optional tessellation stage configuration.
     *
     * Both control and evaluation shaders must be provided for tessellation use.
     */
    struct GFX_API TessellationState
    {
        /** @brief Tessellation control shader (`tesc`). */
        std::reference_wrapper<const Shader> controlShader;

        /** @brief Tessellation evaluation shader (`tese`). */
        std::reference_wrapper<const Shader> evalShader;

        /** @brief Number of control points per patch primitive. */
        glm::u32 patchControlPoints = 3;
    };

    /**
     * @brief Color blending configuration for the pipeline.
     */
    struct GFX_API ColorBlendState
    {
        /**
         * @brief Per-color-attachment blend state.
         */
        struct GFX_API AttachmentState
        {
            /** @brief Enables blending for this attachment. */
            bool blendEnable = false;

            /** @brief Source factor used for RGB blend equation. */
            BlendFactor srcColorBlendFactor = BlendFactor::eOne;

            /** @brief Destination factor used for RGB blend equation. */
            BlendFactor dstColorBlendFactor = BlendFactor::eZero;

            /** @brief Blend operation used for RGB channels. */
            BlendOp colorBlendOp = BlendOp::eAdd;

            /** @brief Source factor used for alpha blend equation. */
            BlendFactor srcAlphaBlendFactor = BlendFactor::eOne;

            /** @brief Destination factor used for alpha blend equation. */
            BlendFactor dstAlphaBlendFactor = BlendFactor::eZero;

            /** @brief Blend operation used for alpha channel. */
            BlendOp alphaBlendOp = BlendOp::eAdd;

            /** @brief Write mask for RGBA output components. */
            Flags<ColorComponent> colorWriteMask =
                Flags(ColorComponent::eR) | ColorComponent::eG | ColorComponent::eB | ColorComponent::eA;
        };

        /** @brief Enables fixed-function logic operation instead of standard blending. */
        bool enableLogicOp = false;

        /** @brief Logic operation applied when `enableLogicOp` is true. */
        LogicOp logicOp = LogicOp::eCopy;

        /** @brief Per-attachment blend states; size should match framebuffer color attachment count. */
        std::vector<AttachmentState> attachments = {};

        /** @brief Blend constants used by blend factors that reference constants. */
        float blendConstants[4] = { 0.f, 0.f, 0.f, 0.f };
    };

    /**
     * @brief Abstract graphics pipeline interface.
     *
     * Stores immutable pipeline state and shader stage references used by backend
     * implementations. Pipelines are created via `GraphicsPipeline::Builder`.
     */
    class GFX_API GraphicsPipeline
    {
    public:
        /**
         * @brief Builder for creating a configured graphics pipeline instance.
         *
         * Populate required shader stages and fixed-function state, then call `build()`.
         * Shader and framebuffer references must outlive the resulting pipeline.
         */
        struct GFX_API Builder {
            /** @brief Vertex shader stage (`vert`). */
            std::optional<std::reference_wrapper<const Shader>> vertexShader = std::nullopt;

            /** @brief Optional tessellation state (control + evaluation stages). */
            std::optional<TessellationState> tessellationState = std::nullopt;

            /** @brief Optional geometry shader stage (`geom`). */
            std::optional<std::reference_wrapper<const Shader>> geometryShader = std::nullopt;

            /** @brief Fragment shader stage (`frag`). */
            std::optional<std::reference_wrapper<const Shader>> fragmentShader = std::nullopt;

            /** @brief Optional task shader stage (mesh shading path). */
            std::optional<std::reference_wrapper<const Shader>> taskShader = std::nullopt;

            /** @brief Optional mesh shader stage (mesh shading path). */
            std::optional<std::reference_wrapper<const Shader>> meshShader = std::nullopt;

            /** @brief Target framebuffer description for this pipeline. */
            std::optional<std::reference_wrapper<const Framebuffer>> framebuffer = std::nullopt;

            /** @brief Vertex attribute layout consumed by the vertex stage. */
            std::vector<VertexInputAttributeDescription> vertexAttributeDescriptions = {};

            /** @brief Vertex binding layout (stride/input rate). */
            std::vector<VertexInputBindingDescription> vertexBindingDescriptions = {};

            /** @brief Primitive assembly state (topology, restart, etc.). */
            InputAssemblyState inputAssemblyState = {};

            /** @brief Rasterization state (polygon mode, culling, front face, etc.). */
            RasterizationState rasterizationState = {};

            /** @brief Multisampling state. */
            MultisampleState multisampleState = {};

            /** @brief Depth/stencil test and write state. */
            DepthStencilState depthStencilState = {};

            /** @brief Color blend state for framebuffer attachments. */
            ColorBlendState colorBlendState = {};

            /**
             * @brief Set vertex shader without modifying vertex input descriptions.
             * @param shader Compiled shader object for vertex stage.
             * @return Reference to this builder.
             */
            Builder& setVertexShader(const Shader& shader);

            /**
             * @brief Set vertex shader and infer vertex input layout from mesh type.
             * @tparam T Mesh type exposing `VertexAttributeDescription()` and `VertexBindingDescription()`.
             * @param shader Compiled shader object for vertex stage.
             * @return Reference to this builder.
             */
            template <gfx::MeshType T>
            Builder& setVertexShader(const Shader& shader)
            {
                this->vertexShader = std::cref(shader);
                this->vertexAttributeDescriptions = T::VertexAttributeDescription();
                this->vertexBindingDescriptions = T::VertexBindingDescription();
                return *this;
            }

            /** @brief Set tessellation state. */
            Builder& setTessellationState(const TessellationState& tessellationState);

            /** @brief Set geometry shader stage. */
            Builder& setGeometryShader(const Shader& geometryShader);

            /** @brief Set fragment shader stage. */
            Builder& setFragmentShader(const Shader& fragmentShader);

            /** @brief Set task shader stage. */
            Builder& setTaskShader(const Shader& taskShader);

            /** @brief Set mesh shader stage. */
            Builder& setMeshShader(const Shader& meshShader);

            /** @brief Override input assembly state. */
            Builder& setInputAssemblyState(const InputAssemblyState& inputAssemblyState);

            /** @brief Override rasterization state. */
            Builder& setRasterizationState(const RasterizationState& rasterizationState);

            /** @brief Override multisample state. */
            Builder& setMultisampleState(const MultisampleState& multisampleState);

            /** @brief Override depth/stencil state. */
            Builder& setDepthStencilState(const DepthStencilState& depthStencilState);

            /** @brief Override color blend state. */
            Builder& setColorBlendState(const ColorBlendState& colorBlendState);

            /** @brief Set target framebuffer. */
            Builder& setFramebuffer(const Framebuffer& framebuffer);

            /**
             * @brief Build a backend-specific graphics pipeline instance.
             * @return Owning pointer to a newly created pipeline.
             */
            [[nodiscard]] std::unique_ptr<GraphicsPipeline> build() const;
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