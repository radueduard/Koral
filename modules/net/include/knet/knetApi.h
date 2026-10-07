//
// koral-net: the module's identity.
//

#pragma once

#include <cstdint>
#include <string_view>

#include "export.h"

namespace knet
{
    /** @brief This module's identity, for another module that declares a dependency on it. */
    inline constexpr std::string_view ModuleId = "koral.net";
    inline constexpr std::uint32_t    ModuleVersion = 1;
}
