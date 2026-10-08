#pragma once

#include <any>
#include <deque>
#include <span>

#include <builderDescriptions.h>

namespace kor::builders::detail
{
    /** @brief The value at @p index, or an empty one past the end: a value not given is zero, or null. */
    inline const Value& At(const std::span<const Value> values, const std::size_t index)
    {
        static const Value empty;
        return index < values.size() ? values[index] : empty;
    }

    /** @brief What a C call's pointers point into, kept until the call returns. */
    class Keep {
    public:
        template <typename T, typename... Args>
        T& Make(Args&&... args)
        {
            return std::any_cast<T&>(_kept.emplace_back(std::in_place_type<T>, std::forward<Args>(args)...));
        }

    private:
        std::deque<std::any> _kept;
    };
}
