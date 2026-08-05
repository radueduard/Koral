//
// Created by radue on 30.07.2026.
//

// The runtime-facing half of the module: identity, and nothing else. Reading a model file needs no
// per-frame hook and caches nothing on the device — what the library holds is Assimp and one factory.

#pragma once

#include <cstdint>
#include <string_view>

#include <module.h>

#include <koralModelImport.h>

namespace kmdl
{
    /** @brief The module: identity, over a library that is otherwise a factory and a pile of templates. */
    class ModelImportModule final : public kor::Module
    {
    public:
        static constexpr std::string_view kModuleId      = kmdl::kModuleId;
        static constexpr std::uint32_t    kModuleVersion = kmdl::kModuleVersion;
    };
}
