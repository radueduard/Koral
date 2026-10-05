/**
 * @file koralFileBrowser.h
 * @brief Picking a file, or a folder, from the disk: the dialog every editor needs once.
 *
 * @code
 * bool _picking = false;
 * kui::Widget Build() override {
 *     return kui::Modal(_picking, Editor(),
 *         kgui::FileBrowser({.directories = true, .createDirectories = true, .confirm = "Choose"},
 *                           [this](const std::filesystem::path& folder) { SetState([&] { _picking = false; Open(folder); }); },
 *                           [this] { SetState([&] { _picking = false; }); }),
 *         [this] { SetState([&] { _picking = false; }); });
 * }
 * @endcode
 *
 * A path along the top to type into or go up from, the folder's contents under it — folders first, each
 * opened with a double click — and a name to pick or type at the foot. Hidden entries (a leading dot)
 * are left out unless asked for.
 */

#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include <kui/widgets.h>

#include "kgui/export.h"

namespace kgui
{
    struct FileBrowserOptions {
        /** Where it opens; the working directory when empty or not a folder. */
        std::filesystem::path start;
        /** Picks a folder rather than a file. */
        bool directories = false;
        /** Files with these extensions only (".png", ".gltf"); every file when empty. Not for folders. */
        std::vector<std::string> extensions;
        /** A "New folder" button, for picking a folder that does not exist yet. */
        bool createDirectories = false;
        bool showHidden = false;
        /** What the button that picks says. */
        std::string confirm = "Open";
        float width = 640.f, height = 440.f;
    };

    /** @brief The browser: @p onChosen is told what was picked, @p onCancelled that nothing was. */
    KGUI_API kui::Widget FileBrowser(FileBrowserOptions options, std::function<void(const std::filesystem::path&)> onChosen,
                                     std::function<void()> onCancelled = {});
}
