#pragma once

// kmath's foundation: the fixed-width aliases the whole API is written in, and what counts as a scalar.

#include <bit>
#include <cmath>
#include <concepts>
#include <cstdint>
#include <limits>
#include <numbers>
#include <type_traits>

namespace kor {
    using u8 = std::uint8_t;
    using u16 = std::uint16_t;
    using u32 = std::uint32_t;
    using u64 = std::uint64_t;
    using i8 = std::int8_t;
    using i16 = std::int16_t;
    using i32 = std::int32_t;
    using i64 = std::int64_t;
    using f32 = float;
    using f64 = double;

    /// The element types a kor::Vec / Mat may hold.
    template<class T>
    concept Scalar = std::is_arithmetic_v<T>;
}
