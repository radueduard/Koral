//
// Created by radue on 11.07.2026.
//

#include "paths.h"

#include <cstdlib>
#include <system_error>

#if defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#else
#  include <dlfcn.h>
#endif

namespace kor::detail
{
    std::filesystem::path moduleDirectory()
    {
        // Deliberately the *library's* location, not the executable's: Koral's shaders and assets are
        // installed alongside Koral, and an application linking it should not have to know that.
        // Both branches ask the loader "which module is this function in?", which is the only way to
        // get an answer that survives being installed anywhere.
#if defined(_WIN32)
        HMODULE module = nullptr;
        if (GetModuleHandleExW(
                GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                reinterpret_cast<LPCWSTR>(&moduleDirectory), &module) && module != nullptr)
        {
            std::wstring buffer(MAX_PATH, L'\0');
            for (;;) {
                const DWORD written = GetModuleFileNameW(module, buffer.data(),
                                                         static_cast<DWORD>(buffer.size()));
                if (written == 0) break;
                if (written < buffer.size()) {
                    buffer.resize(written);
                    return std::filesystem::path(buffer).parent_path();
                }
                buffer.resize(buffer.size() * 2);  // truncated: the path is longer than MAX_PATH
            }
        }
#else
        Dl_info info{};
        if (dladdr(reinterpret_cast<void*>(&moduleDirectory), &info) != 0 && info.dli_fname != nullptr)
        {
            std::error_code ec;
            const auto resolved = std::filesystem::weakly_canonical(
                std::filesystem::path(info.dli_fname), ec);
            if (!ec) return resolved.parent_path();
        }
#endif
        return {};
    }

    std::vector<std::filesystem::path> dataRoots(
        const std::string_view kind, const char* envVar, const std::string_view buildPath)
    {
        std::vector<std::filesystem::path> roots;

        const auto add = [&roots](std::filesystem::path candidate) {
            if (candidate.empty()) return;
            std::error_code ec;
            if (std::filesystem::is_directory(candidate, ec)) roots.push_back(std::move(candidate));
        };

        // 1. An explicit override always wins: it is how a packager relocates the data, and how the
        //    tests point at a fixture directory.
        if (const char* overridden = std::getenv(envVar); overridden != nullptr && *overridden != '\0') {
            add(overridden);
        }

        if (const auto libraryDir = moduleDirectory(); !libraryDir.empty()) {
            // 2. Beside the library — a portable/zip layout, everything in one directory.
            add(libraryDir / kind);
            // 3. The GNU install layout, where the library lands in lib/ and its data in share/.
            add(libraryDir.parent_path() / "share" / "Koral" / kind);
        }

        // 4. The source tree. Last, and only when it is still there: on the machine that built this
        //    it is exactly right, and on any other machine it is an absolute path into nothing.
        //    A distributable build leaves this empty entirely (KORAL_EMBED_SOURCE_PATHS=OFF).
        if (!buildPath.empty()) add(std::filesystem::path(buildPath));

        return roots;
    }
}
