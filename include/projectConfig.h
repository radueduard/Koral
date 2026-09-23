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
 *     "platform": "auto",
 *     "gpu": "radeon",
 *     "window": {
 *       "width": 1280, "height": 720,
 *       "resizable": true, "fullscreen": false, "borderless": false,
 *       "transparent": false, "vsync": true,
 *       "imguiIni": "imgui.ini"
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
 * `"assetDirectories": ["assets"]`, a scene that asks for `kimg::LoadImage("textures/wood.png")`
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
    /**
     * @brief A project's run settings: which window to open, which backend, where its files live.
     *
     * Layered, weakest first — the library's own CreateProjectConfig() is what the project was
     * compiled to want, koral.json is what its author configured without recompiling, and the
     * command-line flags are what this one run overrides. Each layer only replaces the keys it
     * mentions, which is what makes overriding one setting possible without restating the rest.
     *
     * The runtime assembles all three before the window exists. A project embedding Koral directly
     * can use this the same way, or ignore it and configure Window::Builder itself.
     */
    struct KORAL_API ProjectConfig
    {
        /** @brief The file name the runtime searches for. */
        static constexpr std::string_view FileName = "koral.json";

        /** @brief The schema this build understands. Bumped when a key changes meaning. */
        static constexpr int SchemaVersion = 1;

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

        /**
         * @brief Directories searched for the libraries named in @ref modules, most specific first.
         * Koral's own module directory is always searched after these, so a project can override a
         * shipped module by name without losing access to the rest.
         */
        std::vector<std::filesystem::path> moduleDirectories;

        /**
         * @brief The modules to load, by name or by path. @see kor::Module
         *
         * A bare name ("camera") is decorated for the platform and looked up in
         * @ref moduleDirectories; anything with a directory in it is used as written. They are
         * loaded before the device exists and initialized in dependency order, so the order they
         * are listed in does not matter — but every module must be listed, including ones that are
         * only there because another module requires them.
         */
        std::vector<std::string> modules;

        /** @brief Window title. Empty means the engine falls back to the scene library's name. */
        std::string title;

        /** @brief Initial size of the drawable area, in pixels. */
        glm::uvec2 extent = { 1280, 720 };

        /** @brief Which graphics backend to bring up. */
        API api = API::eVulkan;

        /** @brief Which Linux windowing system to open on. Ignored on Windows and macOS. */
        WindowPlatform platform = WindowPlatform::eAuto;

        /**
         * @brief Which GPU the Vulkan backend should use. Empty (the default) keeps the automatic
         * choice — the best suitable device, discrete first. Otherwise an index into the device
         * list the runtime logs at startup, or a case-insensitive substring of a device name
         * ("radeon", "GeForce RTX 4070"). A preference that matches nothing falls back to the
         * automatic choice with a warning. The OpenGL backend cannot choose a device; ignored there.
         */
        std::string gpu;

        /** @brief Whether to open fullscreen on the primary monitor. */
        bool fullscreen = false;

        /** @brief Whether the user may resize the window. */
        bool resizable = false;

        /** @brief Whether the OS draws a title bar and border. */
        bool decorated = true;

        /** @brief Whether the framebuffer's alpha composites with the desktop. */
        bool transparentFramebuffer = false;

        /** @brief Whether presentation waits for the display's refresh. */
        bool vsync = true;

        /**
         * @brief Where Dear ImGui persists its layout (window positions, docking). Empty until a
         * config file is loaded, at which point it defaults to `imgui.ini` beside the config so the
         * layout travels with the project rather than landing in whatever directory the runtime was
         * launched from. `rendering.window.imguiIni` (or `--imgui-ini`) overrides it; a relative
         * override resolves against the config's directory (against the working directory for the
         * flag). When still empty — no config file at all — ImGui keeps its own default.
         */
        std::filesystem::path imguiIni;

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
         * `--width N`, `--height N`, `--title S`, `--api Vulkan|OpenGL`,
         * `--platform auto|x11|wayland` (Linux only), `--gpu INDEX|NAME` (Vulkan only),
         * `--imgui-ini FILE`, `--assets DIR`,
         * `--shaders DIR` (both repeatable, both prepended so the last one given is searched first),
         * and the booleans `--fullscreen`, `--resizable`, `--borderless`, `--transparent`, `--vsync`
         * with their counterparts (`--no-fullscreen`, `--no-resizable`, `--decorated`,
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
