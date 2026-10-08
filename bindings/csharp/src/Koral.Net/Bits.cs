namespace Koral.Net;

/// <summary>
/// knet::BitWriter, in C#: the same bits as C++'s for the same calls, so a message written here reads there.
/// Bit i of the stream is bit (i % 8) of byte (i / 8); values go low bit first.
/// </summary>
public sealed class BitWriter
{
    private readonly List<byte> _bytes = [];
    public long BitCount { get; private set; }

    public void WriteBits(ulong value, int bits)
    {
        for (int i = 0; i < bits; ++i, ++BitCount)
        {
            if ((BitCount & 7) == 0) _bytes.Add(0);
            if (((value >> i) & 1) != 0) _bytes[^1] |= (byte)(1 << (int)(BitCount & 7));
        }
    }
    public void WriteBool(bool value) => WriteBits(value ? 1u : 0u, 1);
    public void WriteU8(byte v) => WriteBits(v, 8);
    public void WriteU16(ushort v) => WriteBits(v, 16);
    public void WriteU32(uint v) => WriteBits(v, 32);
    public void WriteU64(ulong v) => WriteBits(v, 64);
    public void WriteVarUInt(ulong value)
    {
        do
        {
            ulong group = value & 0x7f;
            value >>= 7;
            WriteBits(group | (value != 0 ? 0x80u : 0u), 8);
        } while (value != 0);
    }
    public void WriteVarInt(long value) => WriteVarUInt(((ulong)value << 1) ^ (ulong)(value >> 63));
    public void WriteFloat(float value) => WriteBits(BitConverter.SingleToUInt32Bits(value), 32);
    public void WriteDouble(double value) => WriteBits(BitConverter.DoubleToUInt64Bits(value), 64);
    public void WriteQuantized(float value, float min, float max, int bits)
    {
        ulong steps = (1ul << bits) - 1;
        // std::fmin(std::fmax(v, min), max): a NaN becomes min.
        float clamped = float.IsNaN(value) ? min : Math.Clamp(value, min, max);
        float t = max > min ? (clamped - min) / (max - min) : 0f;
        WriteBits((ulong)Math.Round((double)t * steps, MidpointRounding.AwayFromZero), bits);
    }
    public void WriteString(string text) => WriteBytes(System.Text.Encoding.UTF8.GetBytes(text));
    public void WriteBytes(ReadOnlySpan<byte> bytes)
    {
        WriteVarUInt((ulong)bytes.Length);
        foreach (var b in bytes) WriteBits(b, 8);
    }
    public void WriteVec2(Vec2 v) { WriteFloat(v.X); WriteFloat(v.Y); }
    public void WriteVec3(Vec3 v) { WriteFloat(v.X); WriteFloat(v.Y); WriteFloat(v.Z); }
    public void WriteVec4(Vec4 v) { WriteFloat(v.X); WriteFloat(v.Y); WriteFloat(v.Z); WriteFloat(v.W); }
    /// <summary>Smallest three: 32 bits at the default.</summary>
    public void WriteQuat(Quat q, int bitsPerComponent = 10)
    {
        Span<float> c = [q.X, q.Y, q.Z, q.W];
        int largest = 0;
        for (int i = 1; i < 4; ++i)
            if (MathF.Abs(c[i]) > MathF.Abs(c[largest])) largest = i;
        float sign = c[largest] < 0f ? -1f : 1f;
        WriteBits((ulong)largest, 2);
        const float bound = 0.70710678f;
        for (int i = 0; i < 4; ++i)
            if (i != largest) WriteQuantized(c[i] * sign, -bound, bound, bitsPerComponent);
    }
    public void Align() { while ((BitCount & 7) != 0) WriteBits(0, 1); }

    public byte[] ToArray() => [.. _bytes];
    public void Clear() { _bytes.Clear(); BitCount = 0; }
}

/// <summary>knet::BitReader, in C#. Never reads past its data: a short or malformed message makes it <see cref="Failed"/>, and reads return zeros.</summary>
public sealed class BitReader(byte[] data)
{
    private long _bit;
    public bool Failed { get; private set; }
    public long BitsLeft => Failed ? 0 : data.Length * 8L - _bit;
    public void Fail() => Failed = true;

    public ulong ReadBits(int bits)
    {
        if (Failed || _bit + bits > data.Length * 8L) { Failed = true; return 0; }
        ulong value = 0;
        for (int i = 0; i < bits; ++i, ++_bit)
            if (((data[_bit >> 3] >> (int)(_bit & 7)) & 1) != 0) value |= 1ul << i;
        return value;
    }
    public bool ReadBool() => ReadBits(1) != 0;
    public byte ReadU8() => (byte)ReadBits(8);
    public ushort ReadU16() => (ushort)ReadBits(16);
    public uint ReadU32() => (uint)ReadBits(32);
    public ulong ReadU64() => ReadBits(64);
    public ulong ReadVarUInt()
    {
        ulong value = 0;
        for (int shift = 0; shift < 64; shift += 7)
        {
            ulong group = ReadBits(8);
            value |= (group & 0x7f) << shift;
            if ((group & 0x80) == 0 || Failed) return Failed ? 0 : value;
        }
        Failed = true;
        return 0;
    }
    public long ReadVarInt() { ulong z = ReadVarUInt(); return (long)(z >> 1) ^ -(long)(z & 1); }
    public float ReadFloat() => BitConverter.UInt32BitsToSingle((uint)ReadBits(32));
    public double ReadDouble() => BitConverter.UInt64BitsToDouble(ReadBits(64));
    public float ReadQuantized(float min, float max, int bits)
    {
        ulong steps = (1ul << bits) - 1;
        ulong q = ReadBits(bits);
        return min + (float)((double)q / steps) * (max - min);
    }
    public string ReadString(int limit = 1 << 20) => System.Text.Encoding.UTF8.GetString(ReadBytes(limit));
    public byte[] ReadBytes(int limit = 1 << 26)
    {
        ulong size = ReadVarUInt();
        if (size > (ulong)limit || (long)size * 8 > BitsLeft) { Failed = true; return []; }
        var bytes = new byte[size];
        for (int i = 0; i < bytes.Length; ++i) bytes[i] = (byte)ReadBits(8);
        return bytes;
    }
    public Vec2 ReadVec2() { float x = ReadFloat(); return new Vec2(x, ReadFloat()); }
    public Vec3 ReadVec3() { float x = ReadFloat(), y = ReadFloat(); return new Vec3(x, y, ReadFloat()); }
    public Vec4 ReadVec4() { float x = ReadFloat(), y = ReadFloat(), z = ReadFloat(); return new Vec4(x, y, z, ReadFloat()); }
    public Quat ReadQuat(int bitsPerComponent = 10)
    {
        int largest = (int)ReadBits(2);
        const float bound = 0.70710678f;
        Span<float> c = stackalloc float[4];
        float sum = 0f;
        for (int i = 0; i < 4; ++i)
            if (i != largest) { c[i] = ReadQuantized(-bound, bound, bitsPerComponent); sum += c[i] * c[i]; }
        c[largest] = MathF.Sqrt(MathF.Max(0f, 1f - sum));
        return KMath.Normalize(new Quat(c[0], c[1], c[2], c[3]));
    }
    public void Align()
    {
        while ((_bit & 7) != 0)
        {
            if (_bit >= data.Length * 8L) { Failed = true; return; }
            ++_bit;
        }
    }
}
