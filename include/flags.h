//
// Created by radue on 2/18/2026.
//

#pragma once
#include <type_traits>

namespace kor
{
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
}
