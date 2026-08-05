//
// Created by radue on 29.07.2026.
//

// How this library announces itself, and nothing else.
//
// Linking this module is what makes it load, and loading it is what runs the registrar below —
// which is why a project that uses cameras has nothing to configure. The exported entry points are
// the other route in, for a runtime that loads the module by name.
//
// The module has no lifecycle hooks at all, and the class is here rather than in a header of its own
// because nothing else refers to it. Everything a camera does per frame is driven by the engine's
// repository, which the builders register it with — which is why a camera works identically in a
// headless job, where no module lifecycle runs. What the runtime gets from this library is an
// identity; what a project gets is the API in koralCamera.h.

#include <cstdint>
#include <string_view>

#include <module.h>

#include <koralCamera.h>

namespace kcam
{
    /** @brief The module: identity, over a library that is otherwise builders and a controller. */
    class CameraModule final : public kor::Module
    {
    public:
        static constexpr std::string_view kModuleId      = kcam::kModuleId;
        static constexpr std::uint32_t    kModuleVersion = kcam::kModuleVersion;
    };
}

// No dependencies — cameras need nothing but the engine. A module that needed another would say so
// here with KORAL_DECLARE_MODULE_DEPS, and the loader would order the two accordingly.
KORAL_DECLARE_MODULE(kcam::CameraModule)
