//
// Created by Eduard Andrei Radu on 11.04.2026.
//

module;

#include <api.h>
#include <spdlog/spdlog.h>

export module gfx.log;

namespace gfx::log {
    export void init(); // call once at framework startup

    export template<typename... Args>
    GFX_API void info (spdlog::format_string_t<Args...> fmt, Args&&... args) {
        spdlog::info(fmt, std::forward<Args>(args)...);
    }

    export template<typename... Args>
    GFX_API void warn (spdlog::format_string_t<Args...> fmt, Args&&... args) {
        spdlog::warn(fmt, std::forward<Args>(args)...);
    }

    export template<typename... Args>
    GFX_API void error (spdlog::format_string_t<Args...> fmt, Args&&... args) {
        spdlog::error(fmt, std::forward<Args>(args)...);
    }
}

#ifdef NDEBUG
    #define GFX_ASSERT(condition, msg, ...)                          \
    do { if (!(condition))                                       \
    gfx::log::error("[assert] " msg __VA_OPT__(,) __VA_ARGS__); } while(0)
#else
    #ifdef _MSC_VER
        #define GFX_BREAK() __debugbreak()
    #else
        #define GFX_BREAK() __builtin_trap()
    #endif

    #define GFX_ASSERT(condition, msg, ...)                          \
    do { if (!(condition)) {                                     \
    gfx::log::error("[assert] " msg __VA_OPT__(,) __VA_ARGS__); \
    GFX_BREAK();                                             \
    }} while(0)
#endif