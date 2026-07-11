//
// Created by radue on 28.06.2026.
//

#pragma once

#include <filesystem>
#include <string>

#include <glm/glm.hpp>

#include "api.h"
#include "context.h" // gfx::API

namespace gfx
{
    // Startup configuration a project hands to the engine. A project's shared library may
    // export `extern "C" gfx::ProjectConfig* CreateProjectConfig()` (separate from
    // CreateScene); the engine reads it before creating the Window. These values are
    // defaults — any matching command-line flag overrides them.
    struct GFX_API ProjectConfig
    {
        // The project's shaders/ folder. Added to the shader search roots (alongside the
        // API's own), so Slang modules and GLSL/SPIR-V paths resolve against it.
        std::filesystem::path shaderDirectory;

        std::string title;                  // empty => engine falls back to the scene name
        glm::uvec2 extent = { 1280, 720 };
        API api = API::eVulkan;
        bool fullscreen = false;
        bool resizable = false;
        bool decorated = true;
        bool transparentFramebuffer = false;
        bool vsync = true;
    };
}
