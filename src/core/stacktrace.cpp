#include "stacktrace.h"

#if !KOR_HAS_STD_STACKTRACE

#include <cstdlib>

#if defined(__has_include) && __has_include(<execinfo.h>)
#include <execinfo.h>
#define KOR_HAS_EXECINFO 1
#else
#define KOR_HAS_EXECINFO 0
#endif

namespace kor
{
    Stacktrace Stacktrace::current([[maybe_unused]] unsigned skip)
    {
        Stacktrace trace;
#if KOR_HAS_EXECINFO
        constexpr int maxFrames = 64;
        void* addrs[maxFrames];
        const int count = ::backtrace(addrs, maxFrames);
        char** symbols = ::backtrace_symbols(addrs, count);
        if (symbols) {
            // +1 to also skip this frame (current()) itself, matching std::stacktrace::current's skip.
            for (int i = static_cast<int>(skip) + 1; i < count; ++i) {
                trace._frames.push_back(Frame{symbols[i]});
            }
            std::free(symbols);
        }
#endif
        return trace;
    }
}

#endif