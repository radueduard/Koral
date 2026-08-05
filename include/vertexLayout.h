//
// Created by radue on 29.07.2026.
//

/**
 * @file vertexLayout.h
 * @brief How a vertex buffer is laid out, described at runtime and matched to a shader by name.
 *
 * A layout says what a vertex holds — an attribute's semantic, its channel type and count, its
 * offset, and which binding it comes from — and says nothing about which shader location reads it.
 * The locations come from the shader itself: a vertex shader that annotates its inputs with
 * semantics is matched to the layout by those names, so moving an attribute, reordering the
 * shader's inputs, or leaving one out cannot silently feed a shader the wrong bytes.
 *
 * @code{.glsl}
 * #pragma mesh(POSITION)
 * layout(location = 0) in vec3 position;
 * #pragma mesh(COLOR)
 * layout(location = 1) in vec4 color;
 * @endcode
 *
 * @code{.slang}
 * float4 vertexMain(float3 position : POSITION, float4 color : COLOR) : SV_Position { ... }
 * @endcode
 *
 * @code
 * pipelineBuilder.setVertexShader(vert, MyMesh::Layout());
 * @endcode
 *
 * The vocabulary — `POSITION`, `COLOR`, and the rest — is not the engine's. It belongs to whoever
 * describes the vertices, normally the mesh module, which is why an attribute carries the module's
 * name alongside the semantic. The engine only matches the two.
 *
 * A shader that annotates nothing is matched by declaration order instead: attribute *i* of the
 * layout feeds location *i*, which is what a layout meant before semantics existed.
 *
 * @see kor::Mesh::getVertexLayout, kor::GraphicsPipeline::Builder::setVertexShader
 */

#pragma once

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "api.h"
#include "error.h"
#include "structs.h"

namespace kor
{
    class Shader;

    /**
     * @brief A vertex format as a value: its bindings, and the attributes packed into them.
     *
     * Produced by whatever knows the vertex type — a mesh module's format, or a hand-written
     * description — and consumed by the pipeline, which turns it into locations once it has a
     * shader to match it against.
     */
    struct KORAL_API VertexLayout
    {
        /** @brief One attribute of a vertex, named rather than numbered. */
        struct KORAL_API Attribute
        {
            /// What it is: `POSITION`, `NORMAL`, `UV1`. Empty for an attribute that declares
            /// nothing, which can then only be matched by declaration order.
            std::string semantic;
            /// Which vocabulary the semantic is from — the `mesh` of `mesh(POSITION)`. Empty means
            /// "any", so a shader annotated for one module still matches.
            std::string semanticNamespace;

            glm::u32 binding = 0;                       ///< Which vertex buffer it is read from.
            glm::u32 offset = 0;                        ///< Its byte offset within the vertex.
            ChannelType channelType = ChannelType::eFloat;  ///< The element type of one channel.
            glm::u32 channelCount = 0;                  ///< How many channels — 3 for a vec3.
        };

        /**
         * @brief One stage input of a vertex shader, as much of it as matching needs.
         *
         * A view of what reflection found, not a copy of it: valid only for as long as the shader
         * it was read from. @see Shader::InputOutput
         */
        struct KORAL_API ShaderInput
        {
            glm::u32 location = 0;              ///< Where the shader declared it.
            std::string_view name;              ///< Its name in the source, for diagnostics.
            std::string_view semanticNamespace; ///< Which vocabulary it named, if any.
            std::string_view semantic;          ///< What it asked for, or empty if it asked by position.
        };

        std::vector<VertexInputBindingDescription> bindings;    ///< One per vertex buffer, with its stride.
        std::vector<Attribute> attributes;                      ///< The attributes, in declaration order.

        /**
         * @brief Which attribute carries the vertex position, for ray tracing.
         *
         * Left unset the first attribute is taken, which is what a vertex is almost always written
         * as. A format that puts something else first sets this rather than reordering itself.
         */
        std::optional<std::size_t> positionAttribute = std::nullopt;

        /** @brief Whether this layout describes no vertex input at all. */
        [[nodiscard]] bool empty() const { return attributes.empty() && bindings.empty(); }

        /**
         * @brief The position attribute as a vertex-input description, for an acceleration structure build.
         * @return Its binding, offset and channel format, or nullopt when the layout has no
         *         attributes — in which case the geometry cannot be ray traced. The location is
         *         meaningless here and reported as 0.
         */
        [[nodiscard]] std::optional<VertexInputAttributeDescription> position() const;

        /**
         * @brief Matches this layout against a vertex shader's inputs.
         * @param vertexShader The shader whose stage inputs decide the locations.
         * @return One description per attribute the shader reads, or the first mismatch: a
         *         semantic the shader asks for that this layout does not carry, or one asked for
         *         in a different module's name.
         *
         * Shaders that annotate nothing fall back to declaration order, so a layout still works
         * with a shader written before any of this existed.
         */
        [[nodiscard]] Result<std::vector<VertexInputAttributeDescription>> resolve(const Shader& vertexShader) const;

        /**
         * @brief Matches this layout against stage inputs already in hand.
         * @param inputs The shader's stage inputs, in any order.
         *
         * What the overload above does once it has read them off the shader. Separate because the
         * matching is worth testing without a compiled shader to hand.
         */
        [[nodiscard]] Result<std::vector<VertexInputAttributeDescription>> resolve(
            std::span<const ShaderInput> inputs) const;

        /**
         * @brief Adopts this layout as the one to use when a pipeline is given none.
         *
         * The first format a program describes becomes the default, and later calls are ignored —
         * so a project with one vertex format never has to name it, and one with several is never
         * silently switched between them. Called by whatever defines vertex formats; the mesh
         * module does it for every format it defines.
         */
        static void SetDefault(VertexLayout layout);

        /** @brief The default layout, or an empty one when no format has been described. */
        static const VertexLayout& Default();
    };
}
