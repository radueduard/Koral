//
// Created by radue on 12.07.2026.
//

#include "projectConfig.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <fstream>
#include <ranges>
#include <sstream>

#include <rfl.hpp>
#include <rfl/json.hpp>

#include "log.h"
#include "shader.h"

namespace kor
{
    namespace
    {
        // The wire format of koral.json. This is the Hub's project file — the Hub writes it, we
        // read it, and neither of us reads the other's anything. The schema is the whole contract
        // between the two, so it is mirrored here exactly as the Hub's `model.rs` declares it, and
        // changing one side without the other silently breaks every project the Hub launches.
        //
        // Every field is optional, and that is the point: a key the document does not mention must
        // leave the value beneath it alone, or the file could not act as a *layer* over the
        // project's compiled-in defaults — it would reset every option the user did not spell out.
        //
        // The Hub's own keys that mean nothing to us (color, frameworkVersion, kind, libraries) are
        // simply not declared: unknown keys are ignored, which is also what lets a config written by
        // a newer Hub still load in an older runtime.
        struct WindowDocument
        {
            std::optional<std::string> title;   // not written by the Hub; `name` is the default
            std::optional<std::uint32_t> width;
            std::optional<std::uint32_t> height;
            std::optional<bool> resizable;
            std::optional<bool> fullscreen;
            std::optional<bool> borderless;     // the inverse of our `decorated`
            std::optional<bool> transparent;
            std::optional<bool> vsync;
        };

        struct RenderingDocument
        {
            std::optional<std::string> api;
            std::optional<WindowDocument> window;
        };

        struct PathsDocument
        {
            std::optional<std::vector<std::string>> assetDirectories;
            std::optional<std::vector<std::string>> shaderDirectories;

            // The original singular form, kept readable so projects scaffolded before the lists
            // existed still find their content. The list wins when both are present.
            std::optional<std::string> assetsDir;
            std::optional<std::string> shadersDir;
        };

        struct ConfigDocument
        {
            std::optional<int> schemaVersion;
            std::optional<std::string> name;
            std::optional<RenderingDocument> rendering;
            std::optional<PathsDocument> paths;
        };

        std::optional<API> parseApi(std::string_view name)
        {
            // Accept the enumerator spelling too ("eVulkan"), so a value copied out of the C++ enum
            // works as well as the plain name a human or the Hub writes.
            if (name.starts_with('e') || name.starts_with('E')) name.remove_prefix(1);

            const auto equalsIgnoringCase = [name](const std::string_view other) {
                return std::ranges::equal(name, other, [](const char a, const char b) {
                    return std::tolower(static_cast<unsigned char>(a)) ==
                           std::tolower(static_cast<unsigned char>(b));
                });
            };

            if (equalsIgnoringCase("vulkan")) return API::eVulkan;
            if (equalsIgnoringCase("opengl")) return API::eOpenGL;
            return std::nullopt;
        }

        // A directory from a config file is relative to that file, not to whatever directory the
        // user happened to launch from: the config travels with the project, so the paths in it
        // have to mean the same thing wherever the project is checked out or moved to.
        std::filesystem::path resolveAgainst(const std::filesystem::path& base,
                                             const std::filesystem::path& dir)
        {
            if (dir.is_absolute()) return dir.lexically_normal();
            return (base / dir).lexically_normal();
        }

        // The shared body of merge() and mergeFile(). `source` is only ever used to name the config
        // in a diagnostic, so a file reports its own path while a string parsed straight out of
        // memory reports the directory it was resolved against.
        VoidResult mergeInto(ProjectConfig& config, const std::string_view json,
                             const std::filesystem::path& baseDirectory,
                             const std::string& source)
        {
            const auto invalid = [&source](const std::string& detail) {
                return std::unexpected(Error{
                    .code = ErrorCode::eConfigInvalid,
                    .message = std::format("invalid config '{}': {}", source, detail),
                });
            };

            const auto document = rfl::json::read<ConfigDocument>(std::string(json));
            if (!document) return invalid(document.error().what());

            const auto& doc = *document;

            if (doc.schemaVersion && *doc.schemaVersion > ProjectConfig::kSchemaVersion) {
                // Forward-compatible on purpose: a newer file may only *add* keys, which we ignore.
                // Say so once rather than failing, so an old runtime still starts a new project.
                log::warn("[config] '{}' declares schema {}, this build understands {}; "
                          "unknown settings will be ignored",
                          source, *doc.schemaVersion, ProjectConfig::kSchemaVersion);
            }

            // The project's name is the window title unless the file names one explicitly — which
            // the Hub never does, because in its UI the project simply *is* its name.
            if (doc.name) config.title = *doc.name;

            if (doc.rendering) {
                const auto& r = *doc.rendering;

                if (r.api) {
                    const auto parsed = parseApi(*r.api);
                    if (!parsed)
                        return invalid(std::format("'rendering.api' is '{}'; expected 'Vulkan' or 'OpenGL'", *r.api));
                    config.api = *parsed;
                }

                if (r.window) {
                    const auto& w = *r.window;
                    if (w.title)       config.title = *w.title;
                    if (w.width)       config.extent.x = *w.width;
                    if (w.height)      config.extent.y = *w.height;
                    if (w.resizable)   config.resizable = *w.resizable;
                    if (w.fullscreen)  config.fullscreen = *w.fullscreen;
                    if (w.borderless)  config.decorated = !*w.borderless;
                    if (w.transparent) config.transparentFramebuffer = *w.transparent;
                    if (w.vsync)       config.vsync = *w.vsync;
                }
            }

            // A directory list is replaced wholesale rather than appended to. Appending would leave
            // the file unable to *remove* a root the binary hard-coded, and would silently
            // accumulate duplicates every time a config is merged more than once.
            if (doc.paths) {
                const auto& p = *doc.paths;

                const auto resolveAll = [&baseDirectory](const std::vector<std::string>& dirs) {
                    std::vector<std::filesystem::path> resolved;
                    resolved.reserve(dirs.size());
                    for (const auto& dir : dirs)
                        resolved.push_back(resolveAgainst(baseDirectory, dir));
                    return resolved;
                };

                if (p.assetDirectories)   config.assetDirectories  = resolveAll(*p.assetDirectories);
                else if (p.assetsDir)     config.assetDirectories  = resolveAll({ *p.assetsDir });

                if (p.shaderDirectories)  config.shaderDirectories = resolveAll(*p.shaderDirectories);
                else if (p.shadersDir)    config.shaderDirectories = resolveAll({ *p.shadersDir });
            }

            return {};
        }
    }

    VoidResult ProjectConfig::merge(const std::string_view json,
                                    const std::filesystem::path& baseDirectory)
    {
        return mergeInto(*this, json, baseDirectory, baseDirectory.string());
    }

    VoidResult ProjectConfig::mergeFile(const std::filesystem::path& file)
    {
        std::ifstream stream(file, std::ios::binary);
        if (!stream)
            return std::unexpected(Error{
                .code = ErrorCode::eConfigInvalid,
                .message = std::format("config '{}' cannot be opened for reading", file.string()),
            });

        std::ostringstream contents;
        contents << stream.rdbuf();

        return mergeInto(*this, contents.str(), file.parent_path(), file.string());
    }

    VoidResult ProjectConfig::applyOverrides(const std::span<const std::string> args)
    {
        const auto invalid = [](const std::string& detail) {
            return std::unexpected(Error{
                .code = ErrorCode::eInvalidArgument,
                .message = detail,
            });
        };

        for (std::size_t i = 0; i < args.size(); ++i) {
            const std::string& arg = args[i];

            // Every flag that takes a value goes through this, so a trailing "--width" reports a
            // missing value instead of reading off the end of argv.
            const auto value = [&](std::string_view& out) -> bool {
                if (i + 1 >= args.size()) return false;
                out = args[++i];
                return true;
            };

            const auto number = [&](auto& out) -> std::optional<std::string> {
                std::string_view text;
                if (!value(text)) return std::format("missing value for {}", arg);
                std::uint32_t parsed = 0;
                const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), parsed);
                if (ec != std::errc{} || end != text.data() + text.size())
                    return std::format("{} expects a number, got '{}'", arg, text);
                out = parsed;
                return std::nullopt;
            };

            if (arg == "--config") {
                // Consumed by the runtime before we are called — it has to be, since it decides
                // which config this layer is applied on top of. Skip it and its value.
                std::string_view ignored;
                if (!value(ignored)) return invalid("missing value for --config");
            }
            else if (arg == "--fullscreen")     fullscreen = true;
            else if (arg == "--no-fullscreen")  fullscreen = false;
            else if (arg == "--resizable")      resizable = true;
            else if (arg == "--no-resizable")   resizable = false;
            else if (arg == "--borderless")     decorated = false;
            else if (arg == "--decorated")      decorated = true;
            else if (arg == "--transparent")    transparentFramebuffer = true;
            else if (arg == "--no-transparent") transparentFramebuffer = false;
            else if (arg == "--vsync")          vsync = true;
            else if (arg == "--no-vsync")       vsync = false;
            else if (arg == "--width") {
                if (const auto error = number(extent.x)) return invalid(*error);
            }
            else if (arg == "--height") {
                if (const auto error = number(extent.y)) return invalid(*error);
            }
            else if (arg == "--title") {
                std::string_view text;
                if (!value(text)) return invalid("missing value for --title");
                title = text;
            }
            else if (arg == "--api") {
                std::string_view text;
                if (!value(text)) return invalid("missing value for --api");
                const auto parsed = parseApi(text);
                if (!parsed)
                    return invalid(std::format("--api expects 'Vulkan' or 'OpenGL', got '{}'", text));
                api = *parsed;
            }
            else if (arg == "--assets" || arg == "--shaders") {
                std::string_view text;
                if (!value(text)) return invalid(std::format("missing value for {}", arg));

                // A path typed on the command line means what it means *there*, so it resolves
                // against the working directory — unlike the config's, which resolve against the
                // config file.
                std::error_code ec;
                const auto dir = std::filesystem::absolute(std::filesystem::path(text), ec);
                if (ec) return invalid(std::format("{} '{}' is not a usable path", arg, text));

                // Inserted at the front, so a directory named on the command line outranks the
                // config's — and so that among several, the last one typed wins, like every other
                // flag here.
                auto& directories = arg == "--assets" ? assetDirectories : shaderDirectories;
                directories.insert(directories.begin(), dir.lexically_normal());
            }
            else {
                return invalid(std::format("unknown option '{}'", arg));
            }
        }

        return {};
    }

    std::optional<std::filesystem::path> ProjectConfig::find(const std::filesystem::path& startDirectory)
    {
        std::error_code ec;
        auto directory = std::filesystem::weakly_canonical(startDirectory, ec);
        if (ec) directory = startDirectory;

        // Walk up rather than look in one place: we are usually pointed at a scene library sitting
        // in a build directory (<project>/cmake-build-debug/libFoo.so), and the config lives at the
        // project root above it.
        for (; !directory.empty(); directory = directory.parent_path()) {
            if (auto candidate = directory / kFileName; std::filesystem::is_regular_file(candidate, ec))
                return candidate;
            if (directory.parent_path() == directory) break;  // reached the filesystem root
        }
        return std::nullopt;
    }

    void ProjectConfig::registerSearchPaths() const
    {
        const auto warnIfMissing = [](const std::filesystem::path& dir, const std::string_view kind) {
            std::error_code ec;
            if (!std::filesystem::is_directory(dir, ec))
                log::warn("[config] {} directory '{}' does not exist", kind, dir.string());
        };

        // Walked backwards because each root is inserted at the front: reversing here leaves them in
        // the order the config wrote them, with the first one listed searched first. The engine's
        // own roots stay behind them and are never dropped, so a project that shadows one built-in
        // asset does not thereby lose the rest.
        for (const auto& dir : std::views::reverse(assetDirectories)) {
            warnIfMissing(dir, "asset");
            addAssetSearchPath(dir, /*front=*/true);
        }
        for (const auto& dir : std::views::reverse(shaderDirectories)) {
            warnIfMissing(dir, "shader");
            Shader::addSearchPath(dir, /*front=*/true);
        }
    }

    std::string_view ProjectConfig::usage()
    {
        return
            "  --config <file>     Config file to read (default: nearest koral.json above the scene library)\n"
            "  --assets <dir>      Prepend a directory to search for relative asset paths (repeatable)\n"
            "  --shaders <dir>     Prepend a directory to search for relative shader paths (repeatable)\n"
            "  --title <text>      Window title\n"
            "  --width <n>         Window width\n"
            "  --height <n>        Window height\n"
            "  --api <name>        Graphics backend: Vulkan or OpenGL\n"
            "  --fullscreen        Open fullscreen             (--no-fullscreen)\n"
            "  --resizable         Allow the window to resize  (--no-resizable)\n"
            "  --borderless        Drop the window decorations (--decorated)\n"
            "  --transparent       Transparent framebuffer     (--no-transparent)\n"
            "  --vsync             Wait for vertical blank     (--no-vsync)\n";
    }
}
