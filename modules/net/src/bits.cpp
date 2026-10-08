#include "knet/bits.h"

#include <bit>
#include <cmath>
#include <cstring>

namespace knet
{
    void BitWriter::WriteBits(std::uint64_t value, int bits) {
        for (int i = 0; i < bits; ++i, ++_bits) {
            if ((_bits & 7) == 0) _bytes.push_back(std::byte{0});
            if ((value >> i) & 1u) _bytes.back() |= std::byte(1u << (_bits & 7));
        }
    }

    void BitWriter::WriteVarUInt(std::uint64_t value) {
        do {
            const std::uint64_t group = value & 0x7f;
            value >>= 7;
            WriteBits(group | (value ? 0x80u : 0u), 8);
        } while (value);
    }

    void BitWriter::WriteVarInt(std::int64_t value) {
        WriteVarUInt((std::uint64_t(value) << 1) ^ std::uint64_t(value >> 63));
    }

    void BitWriter::WriteFloat(float value) { WriteBits(std::bit_cast<std::uint32_t>(value), 32); }
    void BitWriter::WriteDouble(double value) { WriteBits(std::bit_cast<std::uint64_t>(value), 64); }

    void BitWriter::WriteQuantized(float value, float min, float max, int bits) {
        const std::uint64_t steps = (std::uint64_t(1) << bits) - 1;
        const float t = max > min ? (std::fmin(std::fmax(value, min), max) - min) / (max - min) : 0.f;
        WriteBits(std::uint64_t(std::lround(double(t) * double(steps))), bits);
    }

    void BitWriter::WriteString(std::string_view text) {
        WriteVarUInt(text.size());
        for (const char c : text) WriteBits(std::uint8_t(c), 8);
    }

    void BitWriter::WriteBytes(std::span<const std::byte> bytes) {
        WriteVarUInt(bytes.size());
        for (const std::byte b : bytes) WriteBits(std::uint8_t(b), 8);
    }

    void BitWriter::WriteQuat(const kor::Quat& q, int bitsPerComponent) {
        // Smallest three: drop the largest component (recovered from unit length), sign-flipped to be positive.
        const float c[4] = {q.x, q.y, q.z, q.w};
        int largest = 0;
        for (int i = 1; i < 4; ++i)
            if (std::fabs(c[i]) > std::fabs(c[largest])) largest = i;
        const float sign = c[largest] < 0.f ? -1.f : 1.f;
        WriteBits(std::uint64_t(largest), 2);
        constexpr float bound = 0.70710678f;   // the others are at most 1/sqrt(2)
        for (int i = 0; i < 4; ++i)
            if (i != largest) WriteQuantized(c[i] * sign, -bound, bound, bitsPerComponent);
    }

    void BitWriter::Align() {
        while (_bits & 7) WriteBits(0, 1);
    }

    std::uint64_t BitReader::ReadBits(int bits) {
        if (_failed || _bit + std::size_t(bits) > _data.size() * 8) {
            _failed = true;
            return 0;
        }
        std::uint64_t value = 0;
        for (int i = 0; i < bits; ++i, ++_bit)
            if ((std::uint8_t(_data[_bit >> 3]) >> (_bit & 7)) & 1u) value |= std::uint64_t(1) << i;
        return value;
    }

    std::uint64_t BitReader::ReadVarUInt() {
        std::uint64_t value = 0;
        for (int shift = 0; shift < 64; shift += 7) {
            const std::uint64_t group = ReadBits(8);
            value |= (group & 0x7f) << shift;
            if (!(group & 0x80) || _failed) return _failed ? 0 : value;
        }
        _failed = true;   // more than ten groups: not a varint
        return 0;
    }

    std::int64_t BitReader::ReadVarInt() {
        const std::uint64_t z = ReadVarUInt();
        return std::int64_t(z >> 1) ^ -std::int64_t(z & 1);
    }

    float BitReader::ReadFloat() { return std::bit_cast<float>(std::uint32_t(ReadBits(32))); }
    double BitReader::ReadDouble() { return std::bit_cast<double>(ReadBits(64)); }

    float BitReader::ReadQuantized(float min, float max, int bits) {
        const std::uint64_t steps = (std::uint64_t(1) << bits) - 1;
        const std::uint64_t q = ReadBits(bits);
        return min + float(double(q) / double(steps)) * (max - min);
    }

    std::string BitReader::ReadString(std::size_t limit) {
        const std::uint64_t size = ReadVarUInt();
        if (size > limit || size * 8 > BitsLeft()) { _failed = true; return {}; }
        std::string text(size, '\0');
        for (char& c : text) c = char(ReadBits(8));
        return text;
    }

    Bytes BitReader::ReadBytes(std::size_t limit) {
        const std::uint64_t size = ReadVarUInt();
        if (size > limit || size * 8 > BitsLeft()) { _failed = true; return {}; }
        Bytes bytes(size);
        for (std::byte& b : bytes) b = std::byte(ReadBits(8));
        return bytes;
    }

    kor::Quat BitReader::ReadQuat(int bitsPerComponent) {
        const int largest = int(ReadBits(2));
        constexpr float bound = 0.70710678f;
        float c[4]{};
        float sum = 0.f;
        for (int i = 0; i < 4; ++i)
            if (i != largest) {
                c[i] = ReadQuantized(-bound, bound, bitsPerComponent);
                sum += c[i] * c[i];
            }
        c[largest] = std::sqrt(std::fmax(0.f, 1.f - sum));
        return kor::Normalize(kor::Quat(c[0], c[1], c[2], c[3]));
    }

    void BitReader::Align() {
        while (_bit & 7) {
            if (_bit >= _data.size() * 8) { _failed = true; return; }
            ++_bit;
        }
    }

    // ---- reflected values ------------------------------------------------------------------------------

    void Write(BitWriter& w, const kor::TypeInfo& type, const void* object) {
        using kor::TypeKind;
        auto floats = [&](int n) {
            const float* f = static_cast<const float*>(object);
            for (int i = 0; i < n; ++i) w.WriteFloat(f[i]);
        };
        auto ints = [&](int n) {
            const std::int32_t* v = static_cast<const std::int32_t*>(object);
            for (int i = 0; i < n; ++i) w.WriteVarInt(v[i]);
        };
        auto uints = [&](int n) {
            const std::uint32_t* v = static_cast<const std::uint32_t*>(object);
            for (int i = 0; i < n; ++i) w.WriteVarUInt(v[i]);
        };
        switch (type.kind) {
            case TypeKind::eBool: w.WriteBool(*static_cast<const bool*>(object)); break;
            case TypeKind::eInt8: w.WriteVarInt(*static_cast<const std::int8_t*>(object)); break;
            case TypeKind::eInt16: w.WriteVarInt(*static_cast<const std::int16_t*>(object)); break;
            case TypeKind::eInt32: w.WriteVarInt(*static_cast<const std::int32_t*>(object)); break;
            case TypeKind::eInt64: w.WriteVarInt(*static_cast<const std::int64_t*>(object)); break;
            case TypeKind::eUInt8: w.WriteU8(*static_cast<const std::uint8_t*>(object)); break;
            case TypeKind::eUInt16: w.WriteVarUInt(*static_cast<const std::uint16_t*>(object)); break;
            case TypeKind::eUInt32: w.WriteVarUInt(*static_cast<const std::uint32_t*>(object)); break;
            case TypeKind::eUInt64: w.WriteVarUInt(*static_cast<const std::uint64_t*>(object)); break;
            case TypeKind::eFloat: w.WriteFloat(*static_cast<const float*>(object)); break;
            case TypeKind::eDouble: w.WriteDouble(*static_cast<const double*>(object)); break;
            case TypeKind::eString: w.WriteString(*static_cast<const std::string*>(object)); break;
            case TypeKind::eVec2: floats(2); break;
            case TypeKind::eVec3: floats(3); break;
            case TypeKind::eVec4: floats(4); break;
            case TypeKind::eQuat: floats(4); break;   // exactly: a quantized one is WriteQuat's job, for who wants it
            case TypeKind::eMat4: floats(16); break;
            case TypeKind::eIVec2: ints(2); break;
            case TypeKind::eIVec3: ints(3); break;
            case TypeKind::eIVec4: ints(4); break;
            case TypeKind::eUVec2: uints(2); break;
            case TypeKind::eUVec3: uints(3); break;
            case TypeKind::eUVec4: uints(4); break;
            case TypeKind::eEnum: w.WriteVarInt(type.enumRead(object)); break;
            case TypeKind::eStruct:
                for (const auto& field : type.fields)
                    if (!field.transient) Write(w, *field.type, field.address(const_cast<void*>(object)));
                break;
            case TypeKind::eArray: {
                const std::size_t n = type.arraySize(object);
                w.WriteVarUInt(n);
                for (std::size_t i = 0; i < n; ++i) Write(w, *type.element, type.arrayAt(const_cast<void*>(object), i));
                break;
            }
        }
    }

    bool Read(BitReader& r, const kor::TypeInfo& type, void* object) {
        using kor::TypeKind;
        auto floats = [&](int n) {
            float* f = static_cast<float*>(object);
            for (int i = 0; i < n; ++i) f[i] = r.ReadFloat();
        };
        auto ints = [&](int n) {
            std::int32_t* v = static_cast<std::int32_t*>(object);
            for (int i = 0; i < n; ++i) v[i] = std::int32_t(r.ReadVarInt());
        };
        auto uints = [&](int n) {
            std::uint32_t* v = static_cast<std::uint32_t*>(object);
            for (int i = 0; i < n; ++i) v[i] = std::uint32_t(r.ReadVarUInt());
        };
        switch (type.kind) {
            case TypeKind::eBool: *static_cast<bool*>(object) = r.ReadBool(); break;
            case TypeKind::eInt8: *static_cast<std::int8_t*>(object) = std::int8_t(r.ReadVarInt()); break;
            case TypeKind::eInt16: *static_cast<std::int16_t*>(object) = std::int16_t(r.ReadVarInt()); break;
            case TypeKind::eInt32: *static_cast<std::int32_t*>(object) = std::int32_t(r.ReadVarInt()); break;
            case TypeKind::eInt64: *static_cast<std::int64_t*>(object) = r.ReadVarInt(); break;
            case TypeKind::eUInt8: *static_cast<std::uint8_t*>(object) = r.ReadU8(); break;
            case TypeKind::eUInt16: *static_cast<std::uint16_t*>(object) = std::uint16_t(r.ReadVarUInt()); break;
            case TypeKind::eUInt32: *static_cast<std::uint32_t*>(object) = std::uint32_t(r.ReadVarUInt()); break;
            case TypeKind::eUInt64: *static_cast<std::uint64_t*>(object) = r.ReadVarUInt(); break;
            case TypeKind::eFloat: *static_cast<float*>(object) = r.ReadFloat(); break;
            case TypeKind::eDouble: *static_cast<double*>(object) = r.ReadDouble(); break;
            case TypeKind::eString: *static_cast<std::string*>(object) = r.ReadString(); break;
            case TypeKind::eVec2: floats(2); break;
            case TypeKind::eVec3: floats(3); break;
            case TypeKind::eVec4: floats(4); break;
            case TypeKind::eQuat: floats(4); break;
            case TypeKind::eMat4: floats(16); break;
            case TypeKind::eIVec2: ints(2); break;
            case TypeKind::eIVec3: ints(3); break;
            case TypeKind::eIVec4: ints(4); break;
            case TypeKind::eUVec2: uints(2); break;
            case TypeKind::eUVec3: uints(3); break;
            case TypeKind::eUVec4: uints(4); break;
            case TypeKind::eEnum: type.enumWrite(object, r.ReadVarInt()); break;
            case TypeKind::eStruct:
                for (const auto& field : type.fields)
                    if (!field.transient && !Read(r, *field.type, field.address(object))) return false;
                break;
            case TypeKind::eArray: {
                const std::uint64_t n = r.ReadVarUInt();
                if (n > r.BitsLeft()) { r.Fail(); return false; }   // every element takes at least a bit
                type.arrayResize(object, std::size_t(n));
                for (std::size_t i = 0; i < n; ++i)
                    if (!Read(r, *type.element, type.arrayAt(object, i))) return false;
                break;
            }
        }
        return !r.Failed();
    }
}
