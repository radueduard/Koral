//
// Created by radue on 13.08.2026.
//

/**
 * @file shaderValue.h
 * @brief The shape of a C++ value, so it can be written into the layout a shader declared.
 *
 * A shader lays its fields out by its own rules — a `mat3` reserves three columns of four floats,
 * an array of `vec3` strides sixteen bytes per element — and C++ lays the same values out tightly.
 * Copying one over the other is what makes half a matrix arrive. This is the description that lets
 * the engine copy it *element by element* into the declared layout instead, so neither side has to
 * know about the other's padding.
 *
 * @see CommandBuffer::PushConstant, semantics.h — which does the same thing for uniform blocks
 */

#pragma once

#include <array>
#include <cstdint>
#include <type_traits>

#include <glm/glm.hpp>

#include "api.h"

namespace kor
{
    /**
     * @brief The scalar a value is made of.
     *
     * Numbered to match Shader::BlockMember::scalar and SemanticSlot::Scalar, so the shader's
     * reflected type and a C++ type can be compared directly.
     */
    enum class ValueScalar : std::uint8_t { eFloat, eInt, eUInt, eBool, eDouble, eOther };

    /** @brief What a value is, in the terms a shader field is described in. */
    struct ValueShape
    {
        ValueScalar scalar = ValueScalar::eOther;
        std::uint8_t rows = 1;      ///< Vector components, or matrix rows. 1 for a scalar.
        std::uint8_t columns = 1;   ///< Matrix columns. 1 for scalars and vectors.
        std::uint32_t count = 1;    ///< Array elements. 1 for a value that is not an array.
        bool known = false;         ///< Whether this is a shape the engine can lay out itself.

        /** @brief Bytes one scalar occupies in C++ *and* in the shader — they agree on this much. */
        [[nodiscard]] constexpr std::uint32_t scalarSize() const {
            return scalar == ValueScalar::eDouble ? 8u : 4u;
        }

        /** @brief Whether two shapes describe the same thing, ignoring how either is padded. */
        [[nodiscard]] constexpr bool sameAs(const ValueShape& other) const {
            return scalar == other.scalar && rows == other.rows
                && columns == other.columns && count == other.count;
        }
    };

    /**
     * @brief Maps a C++ type onto its shader shape. Unspecialised types are "unknown".
     *
     * An unknown type is not an error — it is a struct the engine cannot see inside, and is copied
     * as it stands, with its size checked against the field's. Specialise this only to teach the
     * engine a *scalar-like* type; aggregates are addressed field by field instead.
     */
    template<typename T>
    struct ShaderValueTraits { static constexpr ValueShape shape{}; };

    template<> struct ShaderValueTraits<float>         { static constexpr ValueShape shape{ ValueScalar::eFloat,  1, 1, 1, true }; };
    template<> struct ShaderValueTraits<double>        { static constexpr ValueShape shape{ ValueScalar::eDouble, 1, 1, 1, true }; };
    template<> struct ShaderValueTraits<std::int32_t>  { static constexpr ValueShape shape{ ValueScalar::eInt,    1, 1, 1, true }; };
    template<> struct ShaderValueTraits<std::uint32_t> { static constexpr ValueShape shape{ ValueScalar::eUInt,   1, 1, 1, true }; };
    template<> struct ShaderValueTraits<bool>          { static constexpr ValueShape shape{ ValueScalar::eBool,   1, 1, 1, true }; };

    namespace detail
    {
        template<typename T> constexpr ValueScalar scalarOf() {
            if constexpr (std::is_same_v<T, float>)         return ValueScalar::eFloat;
            else if constexpr (std::is_same_v<T, double>)   return ValueScalar::eDouble;
            else if constexpr (std::is_same_v<T, bool>)     return ValueScalar::eBool;
            else if constexpr (std::is_unsigned_v<T>)       return ValueScalar::eUInt;
            else if constexpr (std::is_integral_v<T>)       return ValueScalar::eInt;
            else                                            return ValueScalar::eOther;
        }
    }

    /// Every glm vector: vec2/3/4 and their integer, unsigned, double and boolean forms.
    template<glm::length_t L, typename T, glm::qualifier Q>
    struct ShaderValueTraits<glm::vec<L, T, Q>> {
        static constexpr ValueShape shape{ detail::scalarOf<T>(), static_cast<std::uint8_t>(L), 1, 1,
                                           detail::scalarOf<T>() != ValueScalar::eOther };
    };

    /// Every glm matrix, square or not. C is the column count, R the rows — glm's own order.
    template<glm::length_t C, glm::length_t R, typename T, glm::qualifier Q>
    struct ShaderValueTraits<glm::mat<C, R, T, Q>> {
        static constexpr ValueShape shape{ detail::scalarOf<T>(), static_cast<std::uint8_t>(R),
                                           static_cast<std::uint8_t>(C), 1,
                                           detail::scalarOf<T>() != ValueScalar::eOther };
    };

    /// A std::array of anything the engine already knows: an array of that, N elements long.
    template<typename T, std::size_t N>
    struct ShaderValueTraits<std::array<T, N>> {
        static constexpr ValueShape shape{ ShaderValueTraits<T>::shape.scalar,
                                           ShaderValueTraits<T>::shape.rows,
                                           ShaderValueTraits<T>::shape.columns,
                                           static_cast<std::uint32_t>(N) * ShaderValueTraits<T>::shape.count,
                                           ShaderValueTraits<T>::shape.known };
    };

    /** @brief The shape of @p T, or an unknown shape for a type the engine cannot lay out. */
    template<typename T>
    constexpr ValueShape shapeOf() { return ShaderValueTraits<std::remove_cvref_t<T>>::shape; }
}
