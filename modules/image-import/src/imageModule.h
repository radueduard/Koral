//
// Created by radue on 29.07.2026.
//

// The runtime-facing half of the module. Loading an image needs no per-frame hook, so there is only
// one: the projection pipeline and sampler the equirectangular path caches are GPU resources this
// library owns, and Shutdown is the one place they can be released while the device still exists.

#pragma once

#include <cstdint>
#include <string_view>

#include <module.h>

#include <koralImageImport.h>

namespace kimg
{
    namespace detail { void releaseGpuCache(); }

    /** @brief The module: identity, and the one hook that lets its cached GPU resources go. */
    class ImageImportModule final : public kor::Module
    {
    public:
        static constexpr std::string_view kModuleId      = kimg::kImportModuleId;
        static constexpr std::uint32_t    kModuleVersion = kimg::kImportModuleVersion;

    private:
        // Private on purpose: the runtime dispatches this virtually through kor::Module*, and
        // nothing else can reach it.
        void Shutdown() override { detail::releaseGpuCache(); }
    };
}
