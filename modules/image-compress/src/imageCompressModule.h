//
// Created by radue on 30.07.2026.
//

// The runtime-facing half of the module. Encoding is CPU work with nothing cached and no per-frame
// hook, so this is identity and a declared dependency: it reads its input through the import module,
// and saying so is what makes a missing import module a startup error naming both rather than a
// link failure.

#pragma once

#include <cstdint>
#include <string_view>

#include <module.h>

#include <koralImageCompress.h>

namespace kimg
{
    /** @brief The module: identity, over a library that is otherwise all free functions. */
    class ImageCompressModule final : public kor::Module
    {
    public:
        static constexpr std::string_view kModuleId      = kimg::kCompressModuleId;
        static constexpr std::uint32_t    kModuleVersion = kimg::kCompressModuleVersion;
    };
}
