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
#include <tuple>
#include <cstring>
#include <stdexcept>
#include <type_traits>

#include "descriptorSetLayout.h"
#include "mesh.h"
#include "api.h"
#include "builder.h"
#include "pipeline.h"
#include "shader.h"

namespace kor
{
    class Framebuffer;
    class Shader;
    class CommandBuffer;

    struct KORAL_API TessellationState
    {
        kor::ResourceRef<const Shader> controlShader;
        kor::ResourceRef<const Shader> evalShader;
        glm::u32 patchControlPoints = 3;
    };

    struct KORAL_API ColorBlendState
    {
        struct KORAL_API AttachmentState
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

    class KORAL_API GraphicsPipeline : public Pipeline
    {
    public:
        struct KORAL_API Builder : ::Builder {
            // Repairable: its inputs are a source file (shaders) or lifetime-tracked shader refs
            // (pipelines), so a failure here can be fixed at runtime and retried. See Builder::Recoverable.
            static constexpr bool Recoverable = true;

            std::optional<kor::ResourceRef<const Shader>> vertexShader = std::nullopt;
            std::optional<TessellationState> tessellationState = std::nullopt;
            std::optional<kor::ResourceRef<const Shader>> geometryShader = std::nullopt;
            std::optional<kor::ResourceRef<const Shader>> fragmentShader = std::nullopt;
            std::optional<kor::ResourceRef<const Shader>> taskShader = std::nullopt;
            std::optional<kor::ResourceRef<const Shader>> meshShader = std::nullopt;
            std::optional<kor::ResourceRef<Framebuffer>> framebuffer = std::nullopt;
            std::vector<VertexInputAttributeDescription> vertexAttributeDescriptions = {};
            std::vector<VertexInputBindingDescription> vertexBindingDescriptions = {};
            InputAssemblyState inputAssemblyState = {};
            RasterizationState rasterizationState = {};
            MultisampleState multisampleState = {};
            DepthStencilState depthStencilState = {};
            ColorBlendState colorBlendState = {};

            Builder& setVertexShader(ResourceRef<const Shader> shader);

            template <kor::MeshType T>
            Builder& setVertexShader(ResourceRef<const Shader> shader)
            {
                this->vertexShader = shader;
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
            Builder& setFramebuffer(kor::ResourceRef<Framebuffer> framebuffer);

            // Bakes a specialization constant into every shader stage of this pipeline.
            // Mirrors ComputePipeline::Builder: stages that don't declare a constant with
            // this id simply ignore it, so a single value can be shared across stages.
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

            std::vector<std::tuple<glm::u32, glm::u32, glm::u32>> specConstantsMetadata {};
            std::vector<std::byte> specConstantsData = std::vector<std::byte>(64, static_cast<std::byte>(0));

            /** @brief One build attempt. Internal: prefer build(). */
            [[nodiscard]] Result<std::unique_ptr<GraphicsPipeline>> create() const;
            [[nodiscard]] kor::Resource<GraphicsPipeline> build() const;

        private:
            glm::u32 currentSpecConstantSize = 0;
        };

        /** @brief Virtual destructor for polymorphic ownership. */
        ~GraphicsPipeline() override;

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

        VoidResult Validate() override;

        std::optional<kor::ResourceRef<const Shader>> _vertexShader;
        std::optional<TessellationState> _tessellationState;
        std::optional<kor::ResourceRef<const Shader>> _geometryShader;
        std::optional<kor::ResourceRef<const Shader>> _fragmentShader;

        std::optional<kor::ResourceRef<const Shader>> _taskShader;
        std::optional<kor::ResourceRef<const Shader>> _meshShader;

        kor::ResourceRef<const Framebuffer> _framebuffer;

        InputAssemblyState _inputAssemblyState;
        RasterizationState _rasterizationState;
        MultisampleState _multisampleState;
        DepthStencilState _depthStencilState;
        ColorBlendState _colorBlendState;

        std::optional<std::vector<VertexInputAttributeDescription>> _vertexAttributeDescriptions;
        std::optional<std::vector<VertexInputBindingDescription>> _vertexBindingDescriptions;

        std::vector<std::tuple<glm::u32, glm::u32, glm::u32>> _specConstantsMetadata;
        std::vector<std::byte> _specConstantsData;
    };
}