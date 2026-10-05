//
// How this library announces itself, and nothing else. The same file every module has.
//

#include <cstdint>
#include <string_view>

#include <module.h>

#include <kui/kuiApi.h>

#include "kgui/export.h"

namespace kgui
{
    /** @brief The module: its identity, and that it needs koral-ui, which draws everything here. */
    class GuiExtrasModule final : public kor::Module
    {
    public:
        static constexpr std::string_view ModuleId      = kgui::ModuleId;
        static constexpr std::uint32_t    ModuleVersion = kgui::ModuleVersion;
    };
}

KORAL_DECLARE_MODULE_DEPS(kgui::GuiExtrasModule, kor::Dependency { kui::ModuleId, kui::ModuleVersion })
