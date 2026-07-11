//
// Created by radue on 28.06.2026.
//

#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include <glm/fwd.hpp>

#include "shader.h"

namespace gfx
{
    struct SlangCompileResult
    {
        std::vector<glm::u32> spirv;        ///< SPIR-V for the requested entry point.
        Shader::Stage stage;                ///< Stage auto-detected from the entry point's [shader(...)] attribute.
        std::filesystem::path resolvedPath; ///< The module's resolved source file (for hot-reload tracking).
        std::vector<std::filesystem::path> dependencies; ///< Every source file the module depends on (module + imports), for hot-reload tracking.
    };

    // Thin wrapper over the Slang in-process compiler. Compiles a single entry point of a
    // module to SPIR-V, resolving the module by name across the given search paths. The
    // resulting SPIR-V is reflected by the existing spirv-cross path, so there is one
    // reflection code path for every language.
    class SlangCompiler
    {
    public:
        // Compiles `entry` of `module` to SPIR-V. Throws BackendException(eShaderCompileFailed)
        // on any failure, so callers wrapping construction in guard() surface a gfx::Error.
        static SlangCompileResult Compile(const std::string& module,
                                          const std::string& entry,
                                          const std::vector<std::filesystem::path>& searchPaths);
    };
}
