//
// koral-ui: what crosses out of the module's library.
//

#pragma once

#include <cstdint>
#include <string_view>

#include "export.h"

namespace kui
{
    /** @brief This module's identity, for another module that declares a dependency on it. */
    inline constexpr std::string_view ModuleId = "koral.ui";
    inline constexpr std::uint32_t    ModuleVersion = 1;
}
