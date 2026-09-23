//
// Created by radue on 13.08.2026.
//

/**
 * @file dataRange.h
 * @brief Taking data to upload as any range, rather than as a span the caller has to build.
 *
 * Everything that fills a buffer or an image reads a sequence of elements and copies it to the
 * GPU. Asking for a `std::span` makes every caller write the conversion out — `std::span<const
 * Vertex>(vertices)` — and quietly rules out anything that is not already contiguous. These
 * concepts let those functions say what they actually need: a range of the right element type.
 *
 * @code
 * mesh.makeBuffer(vertices, usage);                    // a vector
 * mesh.makeBuffer(std::array{a, b, c}, usage);         // an array
 * buffer->Write(positions | std::views::transform(toDevice));   // a view, materialised here
 * @endcode
 *
 * A contiguous range is used where it lies; anything else is walked into a temporary first, which
 * is what @ref ContiguousCopy is for. Nothing else in the engine has to know the difference.
 */

#pragma once

#include <concepts>
#include <ranges>
#include <span>
#include <type_traits>
#include <vector>

namespace kor
{
    /**
     * @brief Any range whose elements are @p T — a vector, an array, a span, a lazy view.
     *
     * The element type has to match exactly rather than merely convert: these ranges are uploaded
     * as bytes, so a range of `double` standing in for a buffer of `float` would be a silent
     * reinterpretation rather than a conversion.
     */
    template<typename R, typename T>
    concept RangeOf = std::ranges::input_range<R>
        && std::same_as<std::remove_cvref_t<std::ranges::range_value_t<R>>, std::remove_cv_t<T>>;

    /** @brief A @ref RangeOf whose elements already sit contiguously, so its bytes can be read directly. */
    template<typename R, typename T>
    concept ContiguousRangeOf = RangeOf<R, T>
        && std::ranges::contiguous_range<R> && std::ranges::sized_range<R>;

    /**
     * @brief A range as one contiguous block, copied only when it is not contiguous already.
     *
     * A vector or an array is viewed where it lies and nothing is allocated; a lazy view is walked
     * into a temporary this object owns. Either way @ref view is a span of the elements, valid for
     * as long as this object *and the range it was built from* are.
     */
    template<typename T>
    class ContiguousCopy
    {
    public:
        template<RangeOf<T> R>
        explicit ContiguousCopy(R&& range)
        {
            if constexpr (ContiguousRangeOf<R, T>) {
                _view = std::span<const T>(std::ranges::data(range), std::ranges::size(range));
            } else {
                if constexpr (std::ranges::sized_range<R>) _owned.reserve(std::ranges::size(range));
                for (const auto& element : range) _owned.push_back(element);
                _view = std::span<const T>(_owned);
            }
        }

        ContiguousCopy(const ContiguousCopy&) = delete;
        ContiguousCopy& operator=(const ContiguousCopy&) = delete;

        /** @brief The elements, contiguous. Empty for an empty range. */
        [[nodiscard]] std::span<const T> view() const {
            // Recomputed rather than returned from _view when we own the data: a move would have
            // left the stored span pointing at the old vector's buffer.
            return _owned.empty() ? _view : std::span<const T>(_owned);
        }

    private:
        std::vector<T> _owned;      ///< Filled only when the source range was not contiguous.
        std::span<const T> _view;   ///< Points into the caller's range, or into _owned.
    };
}
