//
// Created by radue on 2/18/2026.
//

#pragma once
#include <type_traits>

namespace kor
{
    /**
     * @brief Opt an enumeration into the bitwise operators below.
     *
     * Specialise it as std::true_type next to the enum:
     *
     * @code
     * enum class CullMode : std::uint8_t { eNone = 0, eFront = 1 << 0, eBack = 1 << 1 };
     * template<> struct enable_flags<CullMode> : std::true_type {};
     * @endcode
     *
     * Opt-in rather than blanket, because the operators below are free templates over *any*
     * enumeration: unconstrained, they would make `a | b` compile for every scoped enum in the
     * program — including the ones where two enumerators ORed together mean nothing.
     *
     * Only these operators are gated. kor::Flags<E> itself is not, because naming the type is
     * already deliberate, and because several of these enums are nested in a class that uses
     * Flags of them in its own body — where no specialisation could be visible yet.
     */
    template <class E> struct enable_flags : std::false_type {};

    /** @brief An enumeration whose enumerators are bits, per @ref enable_flags. */
    template <class E>
    concept FlagEnum = std::is_enum_v<E> && enable_flags<E>::value;

    /**
     * @brief A type-safe set of bit flags built from a scoped enumeration.
     * @tparam Enum The enumeration whose enumerators are the individual bits. Each must be a
     *         distinct power of two.
     *
     * Scoped enums do not support `|` on their own, and unscoped ones lose the type. This keeps
     * both: the bits carry their enum's identity, so a Buffer::Usage cannot be passed where an
     * Image::Usage is expected, and combining them still reads naturally.
     *
     * @code
     * Flags<Buffer::Usage> usage = Buffer::Usage::eVertex | Buffer::Usage::eTransferDst;
     * usage |= Buffer::Usage::eStorage;
     * if (usage & Buffer::Usage::eVertex) { ... }
     * @endcode
     *
     * A single enumerator converts implicitly, so anything taking a Flags also accepts one flag.
     */
    template <typename Enum> requires std::is_enum_v<Enum>
    class Flags
    {
    public:
        /// The integer type the flags are stored in — the enumeration's own underlying type.
        using UnderlyingType = std::underlying_type_t<Enum>;

        /** @brief An empty set, with no bits set. */
        Flags() : _flags(0) {}

        /** @brief A set holding exactly one flag. Implicit, so a single enumerator can be passed as a Flags. */
        Flags(Enum flag) : _flags(static_cast<UnderlyingType>(flag)) {}

        /** @brief Adds a flag to the set. */
        Flags& operator |=(Enum flag) {
            _flags |= static_cast<UnderlyingType>(flag);
            return *this;
        }

        /** @brief Adds every flag of @p other to the set. */
        Flags& operator |= (const Flags& other) {
            _flags |= other._flags;
            return *this;
        }

        /** @brief This set with @p flag added. */
        Flags operator |(Enum flag) const {
            Flags result(*this);
            result |= flag;
            return result;
        }

        /** @brief The union of the two sets. */
        Flags operator |(const Flags& other) const {
            Flags result(*this);
            result |= other;
            return result;
        }

        /** @brief Narrows the set to the bits it shares with @p flag, dropping every other. */
        Flags& operator &=(Enum flag) {
            _flags &= static_cast<UnderlyingType>(flag);
            return *this;
        }

        /** @brief Narrows the set to the bits it shares with @p other. */
        Flags& operator &=(const Flags& other) {
            _flags &= other._flags;
            return *this;
        }

        /**
         * @brief The intersection of the two sets.
         *
         * Note the asymmetry with operator&(Enum), which answers a bool: testing for one flag is
         * the common case and reads better as a condition, while masking against several only
         * makes sense as a set.
         */
        Flags operator &(const Flags& other) const {
            Flags result(*this);
            result &= other;
            return result;
        }

        /** @brief Toggles the bits of @p other in the set. */
        Flags& operator ^=(const Flags& other) {
            _flags ^= other._flags;
            return *this;
        }

        /** @brief The symmetric difference: the flags in one set or the other, but not both. */
        Flags operator ^(const Flags& other) const {
            Flags result(*this);
            result ^= other;
            return result;
        }

        /**
         * @brief Every bit this set does not hold.
         *
         * The complement covers the whole underlying type, not just the enumerators that were
         * defined, so it is only useful as the right-hand side of an `&`.
         */
        Flags operator ~() const {
            Flags result;
            result._flags = static_cast<UnderlyingType>(~_flags);
            return result;
        }

        /**
         * @brief Tests whether a flag is present.
         * @return true if @p flag is in the set.
         *
         * Note that this returns a bool rather than a Flags, so it reads as a test rather than as
         * an intersection.
         */
        bool operator &(Enum flag) const {
            return (_flags & static_cast<UnderlyingType>(flag)) != 0;
        }

        /** @brief Whether both sets hold exactly the same flags. */
        bool operator ==(const Flags& other) const {
            return _flags == other._flags;
        }

        /** @brief Whether the two sets differ. */
        bool operator !=(const Flags& other) const {
            return !(*this == other);
        }

        /** @brief The raw bits, for handing to a backend API that wants an integer. */
        UnderlyingType value() const {
            return _flags;
        }

        /** @brief Implicit conversion to the raw bits, which also makes an empty set test as false. */
        operator UnderlyingType() const
        {
            return _flags;
        }

    private:
        UnderlyingType _flags;
    };

    // Combining two bare enumerators. Free rather than hidden friends of Flags: the operands are
    // enumerators, so ADL never reaches Flags<E> and a friend declared there would be unfindable.
    template <FlagEnum E> Flags<E> operator|(E a, E b) noexcept { return Flags<E>{a} | Flags<E>{b}; }
    template <FlagEnum E> Flags<E> operator&(E a, E b) noexcept { return Flags<E>{a} & Flags<E>{b}; }
    template <FlagEnum E> Flags<E> operator^(E a, E b) noexcept { return Flags<E>{a} ^ Flags<E>{b}; }
    template <FlagEnum E> Flags<E> operator~(E a)      noexcept { return ~Flags<E>{a}; }
}
