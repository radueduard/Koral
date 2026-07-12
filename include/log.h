//
// Created by Eduard Andrei Radu on 11.04.2026.
//

#pragma once

#include <spdlog/spdlog.h>

namespace kor::log {
    void init(); // call once at framework startup

    template<typename... Args>
    void info (spdlog::format_string_t<Args...> fmt, Args&&... args) {
        spdlog::info(fmt, std::forward<Args>(args)...);
    }

    template<typename... Args>
    void warn (spdlog::format_string_t<Args...> fmt, Args&&... args) {
        spdlog::warn(fmt, std::forward<Args>(args)...);
    }

    template<typename... Args>
    void error (spdlog::format_string_t<Args...> fmt, Args&&... args) {
        spdlog::error(fmt, std::forward<Args>(args)...);
    }
}

#ifdef NDEBUG
    #define KORAL_ASSERT(condition, msg, ...)                          \
    do { if (!(condition))                                       \
    kor::log::error("[assert] " msg __VA_OPT__(,) __VA_ARGS__); } while(0)

    #define KORAL_BREAK() []{}();
#else
    #ifdef _MSC_VER
        #define KORAL_BREAK() __debugbreak()
    #else
        #define KORAL_BREAK() __builtin_trap()
    #endif

    #define KORAL_ASSERT(condition, msg, ...)                          \
    do { if (!(condition)) {                                     \
    kor::log::error("[assert] " msg __VA_OPT__(,) __VA_ARGS__); \
    KORAL_BREAK();                                             \
    }} while(0)
#endif