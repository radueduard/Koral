//
// Created by radue on 11.07.2026.
//

/**
 * @file paths.h
 * @brief Locating the shaders and assets that ship with Koral, wherever it happens to be installed.
 *
 * The build bakes in the source-tree paths, which is fine on the machine that did the building and
 * useless anywhere else: a binary shipped to a user would go looking for its shaders under the
 * absolute path of the CI runner that produced it. So the source path is the *last* thing consulted,
 * and only if it still exists.
 *
 * Everything is resolved relative to the Koral shared library itself, not the executable. Koral's
 * shaders ship with Koral, so an application that merely links against it should not have to copy them
 * into its own bin/.
 */

#pragma once

#include <filesystem>
#include <string_view>
#include <vector>

namespace kor::detail
{
    /** @brief Directory containing the Koral shared library. Empty if it cannot be determined. */
    [[nodiscard]] std::filesystem::path moduleDirectory();

    /**
     * @brief Directories to search for Koral's data of a given @p kind ("shaders" / "assets"),
     *        most specific first. Only directories that actually exist are returned.
     *
     * In order:
     *   1. `$<envVar>`                                   — explicit override (packaging, tests)
     *   2. `<libdir>/<kind>`                             — shipped beside the library (portable zip)
     *   3. `<libdir>/../share/Koral/<kind>`       — a GNU-style install (lib/ + share/)
     *   4. @p buildPath                                  — the source tree, dev builds only
     *
     * @param buildPath The path baked in at build time. Pass an empty string to omit it, which is
     *                  what a distributable build does — see the KORAL_EMBED_SOURCE_PATHS option.
     */
    [[nodiscard]] std::vector<std::filesystem::path> dataRoots(
        std::string_view kind, const char* envVar, std::string_view buildPath);

    /**
     * @brief The first existing @p filename found under any of @c dataRoots(kind, envVar, buildPath),
     *        searched in the same most-specific-first order. Empty if none of the roots contain it.
     */
    [[nodiscard]] std::filesystem::path dataFile(
        std::string_view kind, std::string_view filename, const char* envVar, std::string_view buildPath);
}
