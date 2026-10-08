#pragma once

// Packing values into as few bits as they need, for packets: BitWriter and BitReader, and Serialize /
// Deserialize of any reflected type (KORAL_REFLECT) in Koral's own binary form.
//
// The layout is the same in every language binding: bit i of the stream is bit (i % 8) of byte (i / 8), and
// each value is written low bit first. A reader never reads past its data — a short or malformed packet
// makes it Failed() and its reads return zeros — so untrusted bytes are safe to read.
//
// @code
// knet::BitWriter w;
// w.WriteBits(playerIndex, 5);               // 0..31 in five bits
// w.WriteQuantized(health, 0.f, 100.f, 10);  // 10 bits instead of 32
// w.WriteQuat(rotation);                      // smallest-three: 32 bits
// host.Send(server, 1, w.Data());
// @endcode

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <error.h>
#include <kmath/quaternion.h>
#include <reflect.h>

#include "export.h"

namespace knet
{
    using Bytes = std::vector<std::byte>;

    class KNET_API BitWriter {
    public:
        /** @brief The low @p bits (0–64) of @p value. */
        void WriteBits(std::uint64_t value, int bits);
        void WriteBool(bool value) { WriteBits(value ? 1 : 0, 1); }
        void WriteU8(std::uint8_t v) { WriteBits(v, 8); }
        void WriteU16(std::uint16_t v) { WriteBits(v, 16); }
        void WriteU32(std::uint32_t v) { WriteBits(v, 32); }
        void WriteU64(std::uint64_t v) { WriteBits(v, 64); }
        /** @brief 7 bits at a time, small values small: lengths, counts, ids. */
        void WriteVarUInt(std::uint64_t value);
        /** @brief Zig-zag: small magnitudes small, either sign. */
        void WriteVarInt(std::int64_t value);
        void WriteFloat(float value);
        void WriteDouble(double value);
        /** @brief @p value clamped to [min, max] in @p bits bits — the rest of the precision is dropped. */
        void WriteQuantized(float value, float min, float max, int bits);
        void WriteString(std::string_view text);
        void WriteBytes(std::span<const std::byte> bytes);
        void WriteVec2(const kor::Vec2& v) { WriteFloat(v.x); WriteFloat(v.y); }
        void WriteVec3(const kor::Vec3& v) { WriteFloat(v.x); WriteFloat(v.y); WriteFloat(v.z); }
        void WriteVec4(const kor::Vec4& v) { WriteFloat(v.x); WriteFloat(v.y); WriteFloat(v.z); WriteFloat(v.w); }
        /** @brief A unit quaternion as its three smallest components in @p bitsPerComponent bits each, plus 2: 32 bits at the default. */
        void WriteQuat(const kor::Quat& q, int bitsPerComponent = 10);
        /** @brief Pads to the next whole byte. */
        void Align();

        /** @brief The bytes so far (the last one padded with zeros). */
        [[nodiscard]] const Bytes& Data() const { return _bytes; }
        [[nodiscard]] std::size_t BitCount() const { return _bits; }
        void Clear() { _bytes.clear(); _bits = 0; }

    private:
        Bytes _bytes;
        std::size_t _bits = 0;
    };

    class KNET_API BitReader {
    public:
        explicit BitReader(std::span<const std::byte> data) : _data(data) {}

        std::uint64_t ReadBits(int bits);
        bool ReadBool() { return ReadBits(1) != 0; }
        std::uint8_t ReadU8() { return std::uint8_t(ReadBits(8)); }
        std::uint16_t ReadU16() { return std::uint16_t(ReadBits(16)); }
        std::uint32_t ReadU32() { return std::uint32_t(ReadBits(32)); }
        std::uint64_t ReadU64() { return ReadBits(64); }
        std::uint64_t ReadVarUInt();
        std::int64_t ReadVarInt();
        float ReadFloat();
        double ReadDouble();
        float ReadQuantized(float min, float max, int bits);
        /** @brief At most @p limit bytes; a longer one fails the reader. */
        std::string ReadString(std::size_t limit = 1 << 20);
        Bytes ReadBytes(std::size_t limit = 1 << 26);
        kor::Vec2 ReadVec2() { const float x = ReadFloat(); return {x, ReadFloat()}; }
        kor::Vec3 ReadVec3() { const float x = ReadFloat(), y = ReadFloat(); return {x, y, ReadFloat()}; }
        kor::Vec4 ReadVec4() { const float x = ReadFloat(), y = ReadFloat(), z = ReadFloat(); return {x, y, z, ReadFloat()}; }
        kor::Quat ReadQuat(int bitsPerComponent = 10);
        void Align();

        /** @brief A read went past the end, or a length was larger than allowed: what was read is not to be trusted. */
        [[nodiscard]] bool Failed() const { return _failed; }
        [[nodiscard]] std::size_t BitsLeft() const { return _failed ? 0 : _data.size() * 8 - _bit; }
        void Fail() { _failed = true; }

    private:
        std::span<const std::byte> _data;
        std::size_t _bit = 0;
        bool _failed = false;
    };

    /** @brief An object of a reflected type, field by field. */
    KNET_API void Write(BitWriter& writer, const kor::TypeInfo& type, const void* object);
    /** @brief Fills an object of a reflected type; false when the data runs out or does not fit. */
    KNET_API bool Read(BitReader& reader, const kor::TypeInfo& type, void* object);

    /** @brief A reflected value, as bytes. */
    template<class T>
    Bytes Serialize(const T& value) {
        BitWriter writer;
        Write(writer, kor::TypeOf<T>(), &value);
        return writer.Data();
    }
    /** @brief A reflected value back from bytes; false when they do not hold one. */
    template<class T>
    bool Deserialize(std::span<const std::byte> bytes, T& value) {
        BitReader reader(bytes);
        return Read(reader, kor::TypeOf<T>(), &value) && !reader.Failed();
    }
}
