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
#include <source_location>

#include "builder.h"
#include "pipeline.h"
#include "shader.h"

namespace kor
{
    class Framebuffer;
    class Shader;
    class CommandBuffer;

    /**
     * @brief The tessellation stages, for a pipeline that subdivides its patches on the GPU.
     *
     * Requires the topology to be Topology::ePatchList: tessellation consumes patches, not triangles.
     */
    struct KORAL_API TessellationState
    {
        kor::ResourceRef<const Shader> controlShader;   ///< Decides how finely each patch is subdivided.
        kor::ResourceRef<const Shader> evalShader;      ///< Positions each generated vertex.
        glm::u32 patchControlPoints = 3;                ///< Vertices per patch.
    };

    /**
     * @brief How fragments are combined with what is already in the colour attachments.
     *
     * Blending is per attachment, so a pipeline writing several targets can blend one and overwrite
     * another. Left alone, nothing blends: each fragment replaces what it covers.
     */
    struct KORAL_API ColorBlendState
    {
        /**
         * @brief Blending for one colour attachment.
         *
         * The result is `colorBlendOp(src * srcColorBlendFactor, dst * dstColorBlendFactor)`, with
         * alpha computed separately by its own factors and operation. Ordinary alpha blending is
         * srcAlpha / oneMinusSrcAlpha with eAdd; additive is one / one.
         */
        struct KORAL_API AttachmentState
        {
            bool blendEnable = false;                                   ///< Whether to blend at all. Off means the fragment replaces the destination.
            BlendFactor srcColorBlendFactor = BlendFactor::eOne;        ///< What the incoming colour is scaled by.
            BlendFactor dstColorBlendFactor = BlendFactor::eZero;       ///< What the stored colour is scaled by.
            BlendOp colorBlendOp = BlendOp::eAdd;                       ///< How the two scaled colours are combined.
            BlendFactor srcAlphaBlendFactor = BlendFactor::eOne;        ///< What the incoming alpha is scaled by.
            BlendFactor dstAlphaBlendFactor = BlendFactor::eZero;       ///< What the stored alpha is scaled by.
            BlendOp alphaBlendOp = BlendOp::eAdd;                       ///< How the two scaled alphas are combined.
            Flags<ColorComponent> colorWriteMask =                      ///< Which channels may be written at all. Clearing one protects it from every write, blended or not.
                Flags(ColorComponent::eR) | ColorComponent::eG | ColorComponent::eB | ColorComponent::eA;
        };
        bool enableLogicOp = false;                     ///< Whether to apply a bitwise logic operation instead of blending. Integer attachments only.
        LogicOp logicOp = LogicOp::eCopy;               ///< The operation applied when enableLogicOp is set.
        std::vector<AttachmentState> attachments = {};  ///< One entry per colour attachment, in the framebuffer's order.
        float blendConstants[4] = { 0.f, 0.f, 0.f, 0.f };   ///< The constant colour used by the eConstantColor factors. Overridable per draw with CommandBuffer::SetBlendConstants.
    };

    /**
     * @brief A compiled graphics pipeline: shaders plus all the fixed-function state a draw needs.
     *
     * Everything a draw does apart from the resources it reads is baked in here — which shaders
     * run, how vertices are laid out, whether depth is tested, how fragments blend. Build one per
     * distinct combination, in Scene::Initialize, and bind it before drawing:
     *
     * @code
     * kor::GraphicsPipeline::Builder builder;
     * auto pipeline = builder
     *     .setVertexShader(vertexShader, MyMesh::Layout())   // vertex layout, matched by semantic
     *     .setFragmentShader(fragmentShader)
     *     .setFramebuffer(framebuffer)
     *     .build();
     * @endcode
     *
     * The descriptor set layouts come from the shaders themselves, by reflection, so they are never
     * restated here. State that can vary per draw — cull mode, depth test, blend constants — has a
     * default baked in and can be overridden on the command buffer without building a second
     * pipeline; see the dynamic-state setters on CommandBuffer.
     *
     * Editing a shader on disk rebuilds the pipeline in place. @see Pipeline
     */
    class KORAL_API GraphicsPipeline : public Pipeline
    {
    public:
        /** @brief Collects the shaders and state a graphics pipeline is compiled from. */
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
            /// What the vertices hold, by name. Turned into locations at build time by matching it
            /// against the vertex shader — and again on every reload, so a shader that moves an
            /// attribute to another location is followed rather than mis-read. @see VertexLayout
            std::optional<VertexLayout> vertexLayout = std::nullopt;
            InputAssemblyState inputAssemblyState = {};
            RasterizationState rasterizationState = {};
            MultisampleState multisampleState = {};
            DepthStencilState depthStencilState = {};
            ColorBlendState colorBlendState = {};

            /**
             * @brief Sets the vertex shader and the layout of the vertices it reads.
             * @param shader The vertex shader.
             * @param layout What a vertex holds — normally a mesh format's own, `MyMesh::Layout()`.
             *
             * The two are matched by semantic: each of the shader's inputs is fed the attribute it
             * asks for by name, so the pipeline and the meshes it draws cannot disagree about which
             * bytes are which. A shader that annotates nothing is matched by declaration order
             * instead. @see VertexLayout
             */
            Builder& setVertexShader(ResourceRef<const Shader> shader, const VertexLayout& layout);

            /**
             * @brief Sets the vertex shader, taking the default vertex layout.
             *
             * For a pipeline whose vertices come from somewhere other than a vertex buffer, and for
             * a program with a single vertex format — the first one described becomes the default.
             * @see VertexLayout::SetDefault
             */
            Builder& setVertexShader(ResourceRef<const Shader> shader);

            /** @brief Adds the tessellation stages. Requires Topology::ePatchList. */
            Builder& setTessellationState(const TessellationState& tessellationState);

            /** @brief Adds a geometry shader, which may emit more primitives than it receives. */
            Builder& setGeometryShader(ResourceRef<const Shader> geometryShader);

            /** @brief Sets the fragment shader, which computes each pixel's output. */
            Builder& setFragmentShader(ResourceRef<const Shader> fragmentShader);

            /** @brief Sets the task shader, which decides how much mesh-shader work to launch. Vulkan only. */
            Builder& setTaskShader(ResourceRef<const Shader> taskShader);

            /**
             * @brief Sets the mesh shader, replacing the vertex-input and vertex-shader stages entirely.
             *
             * A mesh-shader pipeline generates its own primitives, so it needs no vertex layout and
             * is drawn with CommandBuffer::DrawMeshTasks. Vulkan only.
             */
            Builder& setMeshShader(ResourceRef<const Shader> meshShader);

            /** @brief Sets how vertices are assembled into primitives — triangles, lines, points, patches. */
            Builder& setInputAssemblyState(const InputAssemblyState& inputAssemblyState);

            /** @brief Sets rasterization: fill mode, culling, winding, depth bias, line width. */
            Builder& setRasterizationState(const RasterizationState& rasterizationState);

            /** @brief Sets multisampling. Must match the sample count of the framebuffer it renders into. */
            Builder& setMultisampleState(const MultisampleState& multisampleState);

            /** @brief Sets depth and stencil testing. */
            Builder& setDepthStencilState(const DepthStencilState& depthStencilState);

            /** @brief Sets how fragments blend with the colour attachments. */
            Builder& setColorBlendState(const ColorBlendState& colorBlendState);

            /**
             * @brief Sets the framebuffer this pipeline renders into.
             * @param framebuffer The target. Its attachment formats and sample count are compiled
             *        into the pipeline, so it can only be used in a pass on a compatible framebuffer.
             */
            Builder& setFramebuffer(kor::ResourceRef<Framebuffer> framebuffer);

            /**
             * @brief Bakes a specialization constant into every shader stage of this pipeline.
             * @tparam T The constant's type; must be trivially copyable.
             * @param id The constant id the shader declares.
             * @param value The value compiled in.
             *
             * The shader is compiled with the value as a literal, so branches on it fold away and
             * loops over it can unroll — the way to specialise one shader into several pipelines
             * without duplicating its source. Stages that declare no constant with this id ignore
             * it, so one value can be shared across stages.
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

            std::vector<std::tuple<glm::u32, glm::u32, glm::u32>> specConstantsMetadata {};
            std::vector<std::byte> specConstantsData = std::vector<std::byte>(64, static_cast<std::byte>(0));

            /** @brief One build attempt. Internal: prefer build(). */
            [[nodiscard]] Result<std::unique_ptr<GraphicsPipeline>> create() const;

            /**
             * @brief Compiles the pipeline.
             * @return It as a Resource; poisoned rather than thrown when a shader fails to compile
             *         or the state is inconsistent, and repaired automatically when the shader is fixed.
             */
            [[nodiscard]] kor::Resource<GraphicsPipeline> build(std::source_location where = std::source_location::current()) const;

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