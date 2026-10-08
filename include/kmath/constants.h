#pragma once

// glm's gtc/constants: mathematical constants, for float or double.

#include "setup.h"

namespace kor {
    template<std::floating_point T = float> inline constexpr T Pi = std::numbers::pi_v<T>;
    template<std::floating_point T = float> inline constexpr T TwoPi = T(2) * std::numbers::pi_v<T>;
    /// TwoPi by its other name: a full turn.
    template<std::floating_point T = float> inline constexpr T Tau = T(2) * std::numbers::pi_v<T>;
    template<std::floating_point T = float> inline constexpr T HalfPi = std::numbers::pi_v<T> / T(2);
    template<std::floating_point T = float> inline constexpr T QuarterPi = std::numbers::pi_v<T> / T(4);
    template<std::floating_point T = float> inline constexpr T OneOverPi = std::numbers::inv_pi_v<T>;
    template<std::floating_point T = float> inline constexpr T TwoOverPi = T(2) * std::numbers::inv_pi_v<T>;
    template<std::floating_point T = float> inline constexpr T E = std::numbers::e_v<T>;
    template<std::floating_point T = float> inline constexpr T GoldenRatio = std::numbers::phi_v<T>;
    template<std::floating_point T = float> inline constexpr T RootTwo = std::numbers::sqrt2_v<T>;
    template<std::floating_point T = float> inline constexpr T RootThree = std::numbers::sqrt3_v<T>;
    template<std::floating_point T = float> inline constexpr T Ln2 = std::numbers::ln2_v<T>;
    template<std::floating_point T = float> inline constexpr T Ln10 = std::numbers::ln10_v<T>;
    /// The tolerance ApproxEqual and EpsilonEqual use when given none: loose enough for the rounding of a few
    /// operations. (Not glm::epsilon, the machine epsilon: that is std::numeric_limits<T>::epsilon().)
    template<std::floating_point T = float> inline constexpr T Epsilon = T(1e-5);
}
