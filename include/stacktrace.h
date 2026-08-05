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
    /** @brief The captured call stack of a failed build, as std::stacktrace where it is available. */
    using Stacktrace = std::stacktrace;
#else
    /**
     * @brief The captured call stack of a failed build, on toolchains without <stacktrace>.
     *
     * Iterable like std::stacktrace and interchangeable with it at the source level, but each frame
     * carries only a symbol: description() returns it, while source_file() and source_line() are
     * always empty.
     */
    class KORAL_API Stacktrace {
    public:
        /** @brief One frame of the stack. */
        struct Frame {
            std::string symbol; ///< The resolved symbol, or an address when it cannot be resolved.

            /** @brief The frame as text. */
            [[nodiscard]] const std::string& description() const noexcept { return symbol; }
            /** @brief Always empty here; the fallback unwinder has no debug info. */
            [[nodiscard]] static std::string source_file() { return {}; }
            /** @brief Always zero here; the fallback unwinder has no debug info. */
            [[nodiscard]] static std::size_t source_line() noexcept { return 0; }
        };

        using value_type = Frame;   // matches std::stacktrace, so callers can name the frame type

        /** @brief An empty trace. */
        Stacktrace() = default;

        /**
         * @brief Captures the current call stack.
         * @param skip How many innermost frames to leave out, so the capture site itself does not
         *        appear in the trace.
         */
        [[nodiscard]] static Stacktrace current(unsigned skip = 0);

        /** @brief Iterator to the innermost frame. */
        [[nodiscard]] auto begin() const noexcept { return _frames.begin(); }
        /** @brief Iterator past the outermost frame. */
        [[nodiscard]] auto end() const noexcept { return _frames.end(); }

    private:
        std::vector<Frame> _frames;
    };
#endif
}