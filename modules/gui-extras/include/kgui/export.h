//
// koral-gui-extras: what crosses out of the module's library, and its identity.
//

#pragma once

#include <cstdint>
#include <string_view>

/*
 * Koral and its modules build with hidden visibility, so what a consumer calls has to say so. Exported
 * here, imported everywhere else; the module's own build defines KORAL_GUI_EXTRAS_EXPORTS.
 */
#if defined(_WIN32)
#  if defined(KORAL_GUI_EXTRAS_EXPORTS)
#    define KGUI_API __declspec(dllexport)
#  else
#    define KGUI_API __declspec(dllimport)
#  endif
#else
#  define KGUI_API __attribute__((visibility("default")))
#endif

namespace kgui
{
    /** @brief This module's identity, for another module that declares a dependency on it. */
    inline constexpr std::string_view ModuleId = "koral.gui.extras";
    inline constexpr std::uint32_t    ModuleVersion = 2;
}
