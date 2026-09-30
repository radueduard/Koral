using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Text;

namespace Koral.Native;

/// <summary>kor::ClearColor across the C interface: the union the generator leaves to hand.</summary>
[StructLayout(LayoutKind.Explicit, Size = 24)]
internal unsafe struct KoralClearColor
{
    [FieldOffset(0)] public uint scalar_type;
    [FieldOffset(4)] public uint components;
    [FieldOffset(8)] public fixed float f[4];
    [FieldOffset(8)] public fixed int i[4];
    [FieldOffset(8)] public fixed uint u[4];
}

internal static unsafe partial class KoralNative
{
    public const string Library = "Koral";

    /// <summary>A string Koral returned: valid until the next call on this thread, so copied at once.</summary>
    public static string Text(byte* text) => text is null ? "" : Marshal.PtrToStringUTF8((IntPtr)text) ?? "";

    public static string LastError() => Text(koral_last_error());

    /// <summary>Throws what the last call on this thread failed with, if it did.</summary>
    public static void Check()
    {
        var error = koral_last_error();
        if (error is not null && *error != 0) throw new KoralException(Text(error));
    }

    public static void Check(int status, [CallerArgumentExpression(nameof(status))] string? call = null)
    {
        if (status != 0) throw new KoralException(LastError() is { Length: > 0 } e ? e : $"{call} failed");
    }

    public static IntPtr Check(IntPtr handle, [CallerArgumentExpression(nameof(handle))] string? call = null)
    {
        if (handle == IntPtr.Zero) throw new KoralException(LastError() is { Length: > 0 } e ? e : $"{call} failed");
        return handle;
    }

    public static byte Bool(bool value) => value ? (byte)1 : (byte)0;

    /// <summary>A UTF-8 copy of <paramref name="text"/> for a struct field, freed with <see cref="Free"/>.</summary>
    public static IntPtr Utf8(string? text) => text is null ? IntPtr.Zero : Marshal.StringToCoTaskMemUTF8(text);
    public static void Free(IntPtr text) => Marshal.FreeCoTaskMem(text);
}
