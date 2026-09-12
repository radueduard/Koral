//
// Created by radue on 29.07.2026.
//

// The runtime-facing half of the module. There is deliberately almost nothing here: a vertex format
// describes itself the first time it is used, and the engine reads that description through
// kor::VertexLayout, so nothing about a mesh needs a per-frame hook or a service object. What the
// module is *for* is identity — a version another module can depend on, and a library whose
// presence in the link is what makes the format vocabulary available.

#pragma once

#include <cstdint>

#include <module.h>

#include <koralMesh.h>

namespace kmesh
{
    /** @brief The module: identity and lifecycle, over a layer that is otherwise all templates. */
    class MeshModule final : public kor::Module
    {
    public:
        static constexpr std::string_view ModuleId      = kmesh::ModuleId;
        static constexpr std::uint32_t    ModuleVersion = kmesh::ModuleVersion;
    };
}
