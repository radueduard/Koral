//
// Created by Eduard Andrei Radu on 11.04.2026.
//

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <spdlog/spdlog.h>

#include "api.h"

/**
 * @brief Logging, with repeat suppression for the messages a frame loop would otherwise flood.
 *
 * Formatting follows std::format, through spdlog:
 *
 * @code
 * kor::log::info("loaded {} meshes in {:.1f} ms", count, elapsed);
 * @endcode
 *
 * Output goes to the default logger the *application* configured. Emission deliberately happens in
 * these header templates rather than inside the library, so a message logged by Koral and one
 * logged by a scene land in the same place.
 */
namespace kor::log {
    /** @brief Initialises the default logger. Called once by the runtime at startup. */
    void init();

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

    /** @brief The current repeat limit. */
    [[nodiscard]] KORAL_API std::size_t repeatLimit();

    /** @brief Forget every message seen so far, restoring a full budget to all of them. */
    KORAL_API void resetRepeatCounts();

    /** @brief How serious a recorded message was. */
    enum class Level : std::uint8_t { eInfo, eWarn, eError };

    /** @brief One message, as the history kept it. */
    struct KORAL_API Record
    {
        Level level = Level::eInfo;  ///< How serious it was.
        std::string message;         ///< The rendered text, without decoration.
        double time = 0.0;           ///< Seconds since the first message was recorded.
        /// Monotonic, never reused, and never reset — so a reader that remembers the last one it saw
        /// can ask for only what has arrived since. @see historySince
        std::uint64_t sequence = 0;
    };

    /**
     * @brief Keeps a copy of @p message, so something other than a terminal can show it.
     *
     * Called by the emission templates below; there is no reason to call it by hand. It exists
     * because a log *panel* cannot be built any other way: spdlog's registry is a static, so every
     * library in the process has its own default logger and a sink attached from one of them would
     * see only that one's messages. This buffer lives in libKoral behind an exported function, so
     * every image — the runtime, a scene, a module — records into the same one.
     *
     * Thread-safe: background threads log too.
     */
    KORAL_API void record(Level level, std::string message);

    /**
     * @brief A copy of the messages still in the history, oldest first.
     *
     * A copy rather than a view, because the buffer is shared with whatever threads are logging.
     *
     * @warning This copies every message, strings and all. Calling it once a frame is what made a log
     *          panel cost most of a frame at a few hundred messages; a reader drawn every frame wants
     *          @ref historySince instead.
     */
    [[nodiscard]] KORAL_API std::vector<Record> history();

    /**
     * @brief Only the messages recorded after @p sequence, oldest first.
     * @param sequence The sequence number of the last record the caller already has; 0 for all of them.
     * @return What has arrived since, which on most frames is nothing at all.
     *
     * What a panel drawn every frame should use: it keeps what it has already been given and asks only
     * for the new, so a quiet frame costs a lock and a comparison rather than a copy of the whole log.
     */
    [[nodiscard]] KORAL_API std::vector<Record> historySince(std::uint64_t sequence);

    /** @brief The sequence number of the most recent record, or 0 if nothing has been logged. */
    [[nodiscard]] KORAL_API std::uint64_t lastSequence();

    /** @brief How many messages are kept. Older ones are dropped; 0 disables the history entirely. */
    KORAL_API void setHistoryLimit(std::size_t limit);

    /** @brief The current history limit. Defaults to 2048. */
    [[nodiscard]] KORAL_API std::size_t historyLimit();

    /** @brief Throws away every message in the history. */
    KORAL_API void clearHistory();

    /**
     * @brief Logs an informational message.
     * @param fmt A std::format-style format string, checked at compile time.
     * @param args Values for its placeholders.
     *
     * Not repeat-suppressed: info is not what floods a log, and rendering every message eagerly to
     * find out would cost more than it saves.
     */
    template<typename... Args>
    void info (spdlog::format_string_t<Args...> fmt, Args&&... args) {
        auto message = fmt::format(fmt, std::forward<Args>(args)...);
        record(Level::eInfo, message);
        spdlog::info("{}", message);
    }

    /** @brief The note appended to the final showing of a recurring message. */
    [[nodiscard]] KORAL_API std::string suppressionNotice();

    /**
     * @brief Logs a warning: something is wrong but the frame can continue.
     * @param fmt A std::format-style format string, checked at compile time.
     * @param args Values for its placeholders.
     *
     * Repeat-suppressed. The message is rendered first so suppression can key on the final text,
     * which means two warnings differing only in a resource name are counted separately.
     */
    template<typename... Args>
    void warn (spdlog::format_string_t<Args...> fmt, Args&&... args) {
        const auto message = fmt::format(fmt, std::forward<Args>(args)...);
        // Recorded on exactly the occasions it is shown, so the panel and the terminal agree about
        // what happened — including agreeing that a flood was suppressed.
        switch (track(message)) {
        case Repeat::eShow:     record(Level::eWarn, message); spdlog::warn("{}", message); break;
        case Repeat::eShowLast: record(Level::eWarn, message); spdlog::warn("{}\n{}", message, suppressionNotice()); break;
        case Repeat::eSuppress: break;
        }
    }

    /**
     * @brief Logs an error: something failed.
     * @param fmt A std::format-style format string, checked at compile time.
     * @param args Values for its placeholders.
     *
     * Repeat-suppressed on the rendered text, exactly as warn() is.
     */
    template<typename... Args>
    void error (spdlog::format_string_t<Args...> fmt, Args&&... args) {
        const auto message = fmt::format(fmt, std::forward<Args>(args)...);
        switch (track(message)) {
        case Repeat::eShow:     record(Level::eError, message); spdlog::error("{}", message); break;
        case Repeat::eShowLast: record(Level::eError, message); spdlog::error("{}\n{}", message, suppressionNotice()); break;
        case Repeat::eSuppress: break;
        }
    }
}

/**
 * @def KORAL_ASSERT(condition, msg, ...)
 * @brief Logs an error when @p condition is false, and breaks into the debugger in a debug build.
 *
 * The message is a format string with its arguments, like the log functions. In a release build
 * (NDEBUG) the error is still logged but execution continues, so an assertion is a diagnostic
 * rather than a crash.
 */
/**
 * @def KORAL_BREAK()
 * @brief Breaks into the attached debugger. A no-op in a release build.
 */
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