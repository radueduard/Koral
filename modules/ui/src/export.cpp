//
// koral-ui: how the library announces itself. Linking it is what loads it. Its one hook releases the
// glyph atlas every font shares — a GPU image that must go before the device does. A UI is drawn by the
// passes a scene adds, not by the module.
//

#include <cstdint>
#include <string_view>

#include <module.h>

#include <kui/api.h>

#include "atlas.h"

namespace kui
{
    class UiModule final : public kor::Module
    {
    public:
        static constexpr std::string_view ModuleId      = kui::ModuleId;
        static constexpr std::uint32_t    ModuleVersion = kui::ModuleVersion;

    private:
        void Shutdown() override { detail::ReleaseAtlas(); }
    };
}

KORAL_DECLARE_MODULE(kui::UiModule)
