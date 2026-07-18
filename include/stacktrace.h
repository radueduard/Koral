//
// Created by radue on 16.07.2026.
//

/**
 * @file stacktrace.h
 * @brief A stacktrace type for Builder's diagnostics, backed by std::stacktrace where the
 *        standard library ships it, and by backtrace_symbols(3) where it does not.
 *
 * Apple's libc++ (the Xcode toolchain, through at least Clang 21) does not ship <stacktrace> at
 * all -- unlike libstdc++ and MSVC's STL -- so this picks between the two at compile time via
 * __has_include. The fallback only carries a raw symbol per frame: source_file()/source_line()
 * need a debug-info-aware unwinder that Apple's runtime doesn't expose.
 */

#pragma once

#if defined(__has_include) && __has_include(<stacktrace>)
#define KOR_HAS_STD_STACKTRACE 1
#else
#define KOR_HAS_STD_STACKTRACE 0
#endif

#if KOR_HAS_STD_STACKTRACE
#include <stacktrace>
#else
#include <cstddef>
#include <string>
#include <vector>

#include "api.h"
#endif

namespace kor
{
#if KOR_HAS_STD_STACKTRACE
    using Stacktrace = std::stacktrace;
#else
    class KORAL_API Stacktrace {
    public:
        struct Frame {
            std::string symbol;

            [[nodiscard]] const std::string& description() const noexcept { return symbol; }
            [[nodiscard]] static std::string source_file() { return {}; }
            [[nodiscard]] static std::size_t source_line() noexcept { return 0; }
        };

        using value_type = Frame;   // matches std::stacktrace, so callers can name the frame type

        Stacktrace() = default;

        [[nodiscard]] static Stacktrace current(unsigned skip = 0);

        [[nodiscard]] auto begin() const noexcept { return _frames.begin(); }
        [[nodiscard]] auto end() const noexcept { return _frames.end(); }

    private:
        std::vector<Frame> _frames;
    };
#endif
}