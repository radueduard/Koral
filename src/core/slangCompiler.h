//
// Created by radue on 28.06.2026.
//

#pragma once

#include <filesystem>
#include <string>
#include <map>
#include <vector>

#include <glm/fwd.hpp>

#include "shader.h"

namespace kor
{
    struct SlangCompileResult
    {
        std::vector<glm::u32> spirv;        ///< SPIR-V for the requested entry point.
        Shader::Stage stage;                ///< Stage auto-detected from the entry point's [shader(...)] attribute.
        std::filesystem::path resolvedPath; ///< The module's resolved source file (for hot-reload tracking).
        std::vector<std::filesystem::path> dependencies; ///< Every source file the module depends on (module + imports), for hot-reload tracking.

        /// One field's annotation: the attribute's name is the module, its argument the semantic.
        struct FieldSemantic { std::string moduleName; std::string semantic; };

        /// Field name -> annotation, from the [module("SEMANTIC")] attributes on block members.
        /// Slang keeps user attributes in its reflection but drops `: SEMANTIC` on anything that
        /// is not a varying, which is why the annotation is an attribute at all. @see semantics.h
        std::map<std::string, FieldSemantic> fieldSemantics;

        /// Location -> the `: SEMANTIC` on a stage input. Varyings are the one place Slang keeps a
        /// semantic, so a vertex input needs no attribute — `float3 p : POSITION` says it already.
        /// Keyed by location because the parameter's name is not what reaches the SPIR-V, and the
        /// module name is empty: a bare semantic names no vocabulary. @see vertexLayout.h
        std::map<glm::u32, FieldSemantic> varyingSemantics;
    };

    // Thin wrapper over the Slang in-process compiler. Compiles a single entry point of a
    // module to SPIR-V, resolving the module by name across the given search paths. The
    // resulting SPIR-V is reflected by the existing spirv-cross path, so there is one
    // reflection code path for every language.
    class SlangCompiler
    {
    public:
        // Compiles `entry` of `module` to SPIR-V. Throws BackendException(eShaderCompileFailed)
        // on any failure, so callers wrapping construction in guard() surface a kor::Error.
        static SlangCompileResult Compile(const std::string& module,
                                          const std::string& entry,
                                          const std::vector<std::filesystem::path>& searchPaths);
    };
}
