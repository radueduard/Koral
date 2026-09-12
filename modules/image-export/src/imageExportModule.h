//
// Created by radue on 30.07.2026.
//

// The runtime-facing half of the module. Writing a file needs no per-frame hook and caches nothing,
// so this is identity and nothing else — a version another module can depend on, and a library whose
// presence in the link is what makes the writers available.

#pragma once

#include <cstdint>
#include <string_view>

#include <module.h>

#include <koralImageExport.h>

namespace kimg
{
    /** @brief The module: identity, over a library that is otherwise all free functions. */
    class ImageExportModule final : public kor::Module
    {
    public:
        static constexpr std::string_view ModuleId      = kimg::ExportModuleId;
        static constexpr std::uint32_t    ModuleVersion = kimg::ExportModuleVersion;
    };
}
