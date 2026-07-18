//
// Created by Eduard Andrei Radu on 11.04.2026.
//

#pragma once

#include <cstddef>
#include <string>

#include <spdlog/spdlog.h>

#include "api.h"

namespace kor::log {
    void init(); // call once at framework startup

    /**
     * @brief Repeat suppression for warnings and errors.
     *
     * A problem that lives in the frame loop — dereferencing a poisoned resource, a command
     * that fails to record, a frame that fails to submit — otherwise reports itself on every
     * frame and buries the first, useful report under thousands of identical lines. So an
     * identical rendered message is shown at most @ref repeatLimit times, and the last one
     * carries a note that the rest are being suppressed.
     *
     * Keyed on the rendered text, so distinct problems never share a budget and a message
     * with a varying value in it (a resource name, a size) is counted separately per value.
     */
    enum class Repeat {
        eShow,      ///< Under the limit; log normally.
        eShowLast,  ///< The last permitted showing; log it with the suppression notice attached.
        eSuppress,  ///< Over the limit; drop it.
    };

    /**
     * @brief Record one occurrence of @p message and say what to do with it.
     *
     * Bookkeeping only — it never logs. Emission stays in the header templates below, on the
     * caller's side of the library boundary: spdlog's registry is a static, so a message written
     * from inside libKoral would go to a *different* default logger than the one the application
     * configured, and would vanish from its log.
     */
    [[nodiscard]] KORAL_API Repeat track(const std::string& message);

    /** @brief Whether @p message should be shown at all. Equivalent to track() != eSuppress. */
    [[nodiscard]] KORAL_API bool shouldEmit(const std::string& message);

    /** @brief Times an identical message is shown before suppression. 0 disables suppression. */
    KORAL_API void setRepeatLimit(std::size_t limit);
    [[nodiscard]] KORAL_API std::size_t repeatLimit();

    /** @brief Forget every message seen so far, restoring a full budget to all of them. */
    KORAL_API void resetRepeatCounts();

    template<typename... Args>
    void info (spdlog::format_string_t<Args...> fmt, Args&&... args) {
        spdlog::info(fmt, std::forward<Args>(args)...);
    }

    // The note appended to the final showing of a recurring message.
    [[nodiscard]] KORAL_API std::string suppressionNotice();

    // warn/error render first so suppression can key on the final text; info is left alone
    // because it is not what floods a log, and rendering it eagerly would cost more than it saves.
    template<typename... Args>
    void warn (spdlog::format_string_t<Args...> fmt, Args&&... args) {
        const auto message = fmt::format(fmt, std::forward<Args>(args)...);
        switch (track(message)) {
        case Repeat::eShow:     spdlog::warn("{}", message); break;
        case Repeat::eShowLast: spdlog::warn("{}\n{}", message, suppressionNotice()); break;
        case Repeat::eSuppress: break;
        }
    }

    template<typename... Args>
    void error (spdlog::format_string_t<Args...> fmt, Args&&... args) {
        const auto message = fmt::format(fmt, std::forward<Args>(args)...);
        switch (track(message)) {
        case Repeat::eShow:     spdlog::error("{}", message); break;
        case Repeat::eShowLast: spdlog::error("{}\n{}", message, suppressionNotice()); break;
        case Repeat::eSuppress: break;
        }
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