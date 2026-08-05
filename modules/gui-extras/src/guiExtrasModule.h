//
// Created by radue on 30.07.2026.
//

// The runtime-facing half of the module: identity, and nothing else. The widgets are header-only and
// compile into whoever draws them; this library exists so the module can be seen and versioned.

#pragma once

#include <cstdint>
#include <string_view>

#include <module.h>

namespace kgui
{
    /** @brief This module's identity, for another module that declares a dependency on it. */
    inline constexpr std::string_view kModuleId = "koral.gui.extras";
    inline constexpr std::uint32_t    kModuleVersion = 1;

    /** @brief The module: identity, over a library that is otherwise all headers. */
    class GuiExtrasModule final : public kor::Module
    {
    public:
        static constexpr std::string_view kModuleId      = kgui::kModuleId;
        static constexpr std::uint32_t    kModuleVersion = kgui::kModuleVersion;
    };
}
