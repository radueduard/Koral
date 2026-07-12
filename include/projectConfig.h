//
// Created by radue on 28.06.2026.
//

/**
 * @file projectConfig.h
 * @brief The run parameters of a project: which window to open, and where to look for its files.
 *
 * A project is configured in three layers, each overriding the one before it:
 *
 *   1. **Compiled-in defaults.** A scene library may export
 *      `extern "C" kor::ProjectConfig* CreateProjectConfig()` (separate from CreateScene). This is
 *      the project's own opinion about how it wants to be run, baked into the binary.
 *   2. **The config file** — `koral.json`, next to the project. This is what a user or a tool (the
 *      Hub) edits without recompiling, and it is where the asset and shader directories live.
 *   3. **Command-line flags.** The last word, for overriding one option on one run.
 *
 * The runtime applies them in that order, so a flag beats the file and the file beats the binary.
 *
 * @section koral_json The config file
 *
 * `koral.json` is the Hub's project file. The Hub writes it, the engine reads it, and neither
 * reads anything of the other's — this schema is the entire contract between them.
 *
 * @code{.json}
 * {
 *   "schemaVersion": 1,
 *   "name": "My Game",
 *   "rendering": {
 *     "api": "Vulkan",
 *     "window": {
 *       "width": 1280, "height": 720,
 *       "resizable": true, "fullscreen": false, "borderless": false,
 *       "transparent": false, "vsync": true
 *     }
 *   },
 *   "paths": {
 *     "assetDirectories":  ["assets", "../shared/assets"],
 *     "shaderDirectories": ["shaders"]
 *   }
 * }
 * @endcode
 *
 * The Hub's own keys (`color`, `frameworkVersion`, `kind`, `libraries`) mean nothing to the engine
 * and are ignored, as is any key from a newer Hub than this build knows about.
 *
 * Every key is optional: an absent key leaves the layer beneath it alone. `name` is the window
 * title unless `window.title` overrides it. Directories may be relative, and are resolved against
 * the directory holding the config file — so a project can be moved or checked out anywhere without
 * its config needing an edit.
 *
 * @section koral_json_paths Path resolution
 *
 * The directories are the roots that relative paths are resolved against at load time. Given
 * `"assetDirectories": ["assets"]`, a scene that asks for `Importer::LoadImage("textures/wood.png")`
 * gets `<project>/assets/textures/wood.png` — and if it isn't there, Koral keeps looking through the
 * remaining roots, ending with the assets that ship with the engine itself. Config directories are
 * searched first, so a project can shadow an engine asset by name; it can never lose access to the
 * engine's own. Absolute paths are used exactly as given and never resolved.
 */

#pragma once

#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <glm/glm.hpp>

#include "api.h"
#include "context.h" // kor::API
#include "error.h"

namespace kor
{
    struct KORAL_API ProjectConfig
    {
        /** @brief The file name the runtime searches for. */
        static constexpr std::string_view kFileName = "koral.json";

        /** @brief The schema this build understands. Bumped when a key changes meaning. */
        static constexpr int kSchemaVersion = 1;

        /**
         * @brief Roots for resolving relative asset paths (textures, models), most specific first.
         * Registered ahead of the engine's own assets/ — see @ref registerSearchPaths.
         */
        std::vector<std::filesystem::path> assetDirectories;

        /**
         * @brief Roots for resolving relative shader paths and Slang module imports, most specific
         * first. Registered ahead of the engine's own shaders/.
         */
        std::vector<std::filesystem::path> shaderDirectories;

        std::string title;                  // empty => engine falls back to the scene name
        glm::uvec2 extent = { 1280, 720 };
        API api = API::eVulkan;
        bool fullscreen = false;
        bool resizable = false;
        bool decorated = true;
        bool transparentFramebuffer = false;
        bool vsync = true;

        /**
         * @brief Overlay a config document onto this config.
         *
         * Keys present in @p json replace the current value; absent keys leave it untouched, which
         * is what makes the layering work. Relative directories resolve against @p baseDirectory.
         *
         * @return ErrorCode::eConfigInvalid if the document is not valid JSON or a key has the
         *         wrong type. A config that is merely *empty* is not an error.
         */
        VoidResult merge(std::string_view json, const std::filesystem::path& baseDirectory);

        /** @brief @ref merge the contents of @p file, resolving its directories against its parent. */
        VoidResult mergeFile(const std::filesystem::path& file);

        /**
         * @brief Overlay command-line overrides onto this config.
         *
         * @p args are the arguments alone — no program name, no scene library. Recognised flags:
         * `--width N`, `--height N`, `--title S`, `--api Vulkan|OpenGL`, `--assets DIR`,
         * `--shaders DIR` (both repeatable, both prepended so the last one given is searched
         * first), and the booleans `--fullscreen`, `--resizable`, `--borderless`, `--transparent`,
         * `--vsync` with their counterparts (`--no-fullscreen`, `--no-resizable`, `--decorated`,
         * `--no-transparent`, `--no-vsync`).
         *
         * `--config FILE` is consumed by the runtime before this is called, and is skipped here.
         *
         * @return ErrorCode::eInvalidArgument on an unknown flag, a missing value, or an
         *         unparseable number — a typo'd flag is a mistake worth stopping for, not one to
         *         silently drop on the floor.
         */
        VoidResult applyOverrides(std::span<const std::string> args);

        /**
         * @brief Locate the config file for a project, starting at @p startDirectory and walking up.
         *
         * The runtime is handed a scene *library*, which usually sits in a build directory a level
         * or two below the project root where the config lives — so we walk up rather than demand
         * that the caller know the layout. Stops at the filesystem root.
         */
        static std::optional<std::filesystem::path> find(const std::filesystem::path& startDirectory);

        /**
         * @brief Register @ref assetDirectories and @ref shaderDirectories as search roots, ahead of
         *        the engine's own. Call once, before any resource is loaded.
         */
        void registerSearchPaths() const;

        /** @brief The flags @ref applyOverrides accepts, formatted for a `--help` listing. */
        static std::string_view usage();
    };
}
