//
// Created by radue on 29.07.2026.
//

/**
 * @file semantics.h
 * @brief Letting a shader ask the engine to fill a struct in, field by field, by name.
 *
 * A shader declares what it wants and annotates each field with a *semantic* — a name from a
 * shared vocabulary. Whatever object can answer for that semantic fills it in. The shader decides
 * the layout, the set and the binding; nothing on the C++ side assumes any of them.
 *
 * @code{.slang}
 * import koral;                            // where the Kor attribute is declared
 *
 * struct Frame {
 *     [Kor(KOR_VIEW_PROJECTION_MATRIX)] float4x4 viewProjection;
 *     [Kor(KOR_CAMERA_POSITION)]        float3   eye;
 *     float4x4 myOwnThing;                 // not annotated: never written, yours to fill
 * };
 * ParameterBlock<Frame> frame;
 * @endcode
 *
 * @code{.glsl}
 * layout(set = 1, binding = 0) uniform Frame {
 *     #pragma kor(KOR_VIEW_PROJECTION_MATRIX)
 *     mat4 viewProjection;
 *     #pragma kor(KOR_CAMERA_POSITION)
 *     vec3 eye;
 *     mat4 myOwnThing;
 * };
 * @endcode
 *
 * @code{.cpp}
 * auto set = kor::DescriptorSet::Builder(_pipeline, 1)
 *     .write(0, *_camera)   // the camera answers for every KOR_CAMERA_* / KOR_VIEW_* semantic
 *     .build();
 * @endcode
 *
 * The buffer is created by the write, sized and laid out from the shader's own reflection, and
 * owned by the object that filled it — so two shaders that want different fields of the same
 * camera get two buffers, each exactly the shape its shader declared.
 *
 * @section semantics_errors When it goes wrong
 *
 * A semantic written onto a field of the wrong type — KOR_VIEW_MATRIX on a `float3` — poisons the
 * descriptor set, naming the field, the semantic and both types. That is a mistake in the shader,
 * so it is fixed by editing the shader: the set is rebuilt by the reload system and comes back on
 * its own. A semantic nobody answers for is the same kind of mistake and behaves the same way.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <glm/glm.hpp>

#include "api.h"
#include "error.h"
#include "resource.h"
#include "shader.h"

namespace kor
{
    class Buffer;
    class SemanticSerializer;

    /**
     * @brief One annotated field of a shader block, and the memory it occupies.
     *
     * Handed to a @ref SemanticSerializer so it can fill exactly that field. The type is the
     * shader's, read out of the compiled code — which is what makes a mismatch detectable rather
     * than a silently misread buffer.
     */
    class KORAL_API SemanticSlot
    {
        friend class DescriptorSet;
    public:
        /** @brief The scalar a field is made of. */
        enum class Scalar : std::uint8_t { eFloat, eInt, eUInt, eBool, eDouble, eOther };

        SemanticSlot(std::string_view semantic, std::string_view field,
                     Scalar scalar, std::uint8_t rows, std::uint8_t columns,
                     std::span<std::byte> destination);

        /** @brief The semantic this field was annotated with. */
        [[nodiscard]] std::string_view semantic() const { return _semantic; }

        /** @brief The field's name in the shader, for diagnostics. */
        [[nodiscard]] std::string_view field() const { return _field; }

        /**
         * @brief Writes a value into the field.
         * @return true if it was written; false if the field is not of that shape, in which case
         *         the mismatch is recorded and @ref error explains it.
         *
         * A serializer calls whichever of these matches the value it holds and ignores the result:
         * the recorded error is what the descriptor set reports, all of them at once, rather than
         * each caller having to check.
         */
        bool set(float value);
        bool set(std::int32_t value);
        bool set(std::uint32_t value);
        bool set(const glm::vec2& value);
        bool set(const glm::vec3& value);
        bool set(const glm::vec4& value);
        bool set(const glm::mat3& value);
        bool set(const glm::mat4& value);

        /** @brief Why the last set() was refused, if it was. */
        [[nodiscard]] const std::optional<Error>& error() const { return _error; }

        /** @brief Whether anything has been written here. */
        [[nodiscard]] bool written() const { return _written; }

    private:
        /** @brief Checks the field's shape and copies @p bytes in if it matches. */
        bool write(Scalar scalar, std::uint8_t rows, std::uint8_t columns,
                   const void* bytes, std::size_t size);

        /** @brief "float3", "float4x4" — how a shape is named in a mismatch report. */
        static std::string describe(Scalar scalar, std::uint8_t rows, std::uint8_t columns);

        std::string _semantic;
        std::string _field;
        Scalar _scalar;
        std::uint8_t _rows;
        std::uint8_t _columns;
        std::span<std::byte> _destination;
        std::optional<Error> _error;
        bool _written = false;
    };

    /**
     * @brief The buffers an object has been asked to fill, one per block shape it has seen.
     *
     * A camera bound into two shaders that declare different fields needs two buffers, each laid
     * out the way its own shader declared. This holds them, keyed by the shape of the block, and
     * re-serializes all of them once a frame.
     *
     * Embed one in whatever implements @ref SemanticSerializer and return it from
     * SemanticSerializer::semanticBuffers(); nothing else has to be written.
     */
    class KORAL_API SemanticBuffers
    {
    public:
        SemanticBuffers();
        ~SemanticBuffers();

        SemanticBuffers(const SemanticBuffers&) = delete;
        SemanticBuffers& operator=(const SemanticBuffers&) = delete;

        /**
         * @brief Re-serializes every block this object has been asked for.
         *
         * Call once a frame, from wherever the object already updates itself — a camera does it in
         * its automaticUpdate. Blocks whose bytes have not changed are not re-uploaded.
         */
        void refresh(const SemanticSerializer& owner);

        /** @brief How many distinct block shapes this object has been asked to fill. */
        [[nodiscard]] std::size_t blockCount() const;

        /**
         * @brief The buffer for one block shape, created and filled on first request.
         * @return The buffer, or the first mismatch the shape produced — which is what poisons the
         *         descriptor set that asked for it.
         *
         * Called by DescriptorSet::Builder; there is no reason for anything else to.
         */
        [[nodiscard]] Result<ResourceRef<const Buffer>> acquire(
            const std::vector<Shader::BlockMember>& members, std::uint32_t blockSize,
            const SemanticSerializer& owner);

    private:
        friend class DescriptorSet;

        struct Block;
        struct State;
        std::unique_ptr<State> _state;
    };

    /**
     * @brief Something that can answer for semantics: a camera, a light, a project's own object.
     *
     * Implemented by whatever holds the data a shader wants. The engine never asks what the object
     * *is* — DescriptorSet::Builder::write takes the object itself and asks, at runtime, whether it
     * can serialize; anything that can, can be written into a semantic block.
     *
     * @code
     * class Sun : public kor::SemanticSerializer {
     *     std::string_view semanticNamespace() const override { return "sun"; }
     *     bool serialize(std::string_view semantic, kor::SemanticSlot& slot) const override {
     *         if (semantic == "DIRECTION") { slot.set(_direction); return true; }   // sun(DIRECTION)
     *         return false;
     *     }
     *     kor::SemanticBuffers& semanticBuffers() override { return _buffers; }
     *     kor::SemanticBuffers _buffers;
     * };
     * @endcode
     */
    class KORAL_API SemanticSerializer
    {
    public:
        virtual ~SemanticSerializer() = default;

        /**
         * @brief Fills one field.
         * @param semantic What the shader asked for.
         * @param slot The field to write it into. @see SemanticSlot::set
         * @return Whether this object answers for @p semantic at all. Returning false is what
         *         makes an unanswered semantic a reportable mistake rather than a silent zero.
         */
        virtual bool serialize(std::string_view semantic, SemanticSlot& slot) const = 0;

        /**
         * @brief The name this object answers under — the `camera` of `camera(VIEW_MATRIX)`.
         *
         * Normally the module's own name. It is what lets a block say which module it wants
         * filling it, so writing a light into a block annotated for a camera is caught and named
         * rather than quietly leaving every field at zero.
         */
        [[nodiscard]] virtual std::string_view semanticNamespace() const = 0;

        /** @brief Where the blocks filled from this object live. Normally a member. */
        virtual SemanticBuffers& semanticBuffers() = 0;
    };
}
