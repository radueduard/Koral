//
// Created by radue on 2/18/2026.
//

export module flags;
import std;

export template <typename E> requires std::is_enum_v<E>
class Flags
{
public:
    using UnderlyingType = std::underlying_type_t<E>;

    Flags() : _flags(0) {}

    Flags(E flag) : _flags(static_cast<UnderlyingType>(flag)) {}

    Flags& operator |=(E flag) {
        _flags |= static_cast<UnderlyingType>(flag);
        return *this;
    }

    Flags& operator |= (const Flags& other) {
        _flags |= other._flags;
        return *this;
    }

    Flags operator |(E flag) const {
        Flags result(*this);
        result |= flag;
        return result;
    }

    Flags operator |(const Flags& other) const {
        Flags result(*this);
        result |= other;
        return result;
    }

    Flags& operator &=(E flag) {
        _flags &= static_cast<UnderlyingType>(flag);
        return *this;
    }

    bool operator &(E flag) const {
        return (_flags & static_cast<UnderlyingType>(flag)) != 0;
    }

    bool operator ==(const Flags& other) const {
        return _flags == other._flags;
    }

    bool operator !=(const Flags& other) const {
        return !(*this == other);
    }

    UnderlyingType value() const {
        return _flags;
    }

    operator UnderlyingType() const
    {
        return _flags;
    }

private:
    UnderlyingType _flags;
};
