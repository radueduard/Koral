//
// Reading a whole string as a floating-point number, on every standard library.
//

#pragma once

#include <charconv>
#include <locale>
#include <optional>
#include <cctype>
#include <sstream>
#include <string>
#include <string_view>

namespace kor::detail
{
    /** @brief @p text, all of it, as a @p T; nullopt if it is not exactly one number. Locale-independent. */
    template <typename T>
    std::optional<T> ParseFloating(const std::string_view text)
    {
        T parsed {};
#if defined(__cpp_lib_to_chars) && __cpp_lib_to_chars >= 201611L
        const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), parsed);
        if (ec != std::errc{} || end != text.data() + text.size()) return std::nullopt;
#else
        // libc++ before 20 — Apple's included — has no floating-point from_chars.
        if (text.empty() || std::isspace(static_cast<unsigned char>(text.front()))) return std::nullopt;
        std::istringstream in{std::string(text)};
        in.imbue(std::locale::classic());
        if (!(in >> parsed) || in.peek() != std::char_traits<char>::eof()) return std::nullopt;
#endif
        return parsed;
    }
}
