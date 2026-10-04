using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using Koral.Native;

namespace Koral.UI.Native;

internal static unsafe partial class KuiNative
{
    public const string Library = "koral-ui";

    /// <summary>The file the module is, on this OS.</summary>
    public static string FileName =>
        OperatingSystem.IsWindows() ? "koral-ui.dll" :
        OperatingSystem.IsMacOS() ? "libkoral-ui.dylib" :
        "libkoral-ui.so";

    // Before any P/Invoke in this assembly. The module sits in modules/ beside Koral's own library, wherever
    // that was found; KORAL_UI_LIBRARY names the file itself.
#pragma warning disable CA2255
    [ModuleInitializer]
#pragma warning restore CA2255
    internal static void Register() =>
        NativeLibrary.SetDllImportResolver(typeof(KuiNative).Assembly, Resolve);

    private static IntPtr Resolve(string name, Assembly assembly, DllImportSearchPath? searchPath)
    {
        if (name != Library) return IntPtr.Zero;
        // Koral first: the module links against it, and must find the copy C# already loaded — which, on Windows,
        // takes saying (NativeLibraryResolver.LoadBesideKoral).
        // A file that is there and does not load is said, with why: "not found" would send one looking for it.
        string? unloadable = null;
        foreach (var candidate in Candidates())
        {
            if (!File.Exists(candidate)) continue;
            try
            {
                return NativeLibraryResolver.LoadBesideKoral(candidate);
            }
            catch (Exception e) when (e is DllNotFoundException or BadImageFormatException)
            {
                unloadable ??= $"{candidate} could not be loaded: {e.Message}";
            }
        }
        if (NativeLibrary.TryLoad(name, assembly, searchPath, out var fromSystem)) return fromSystem;
        throw new DllNotFoundException(unloadable ??
            $"koral-ui's native module ({FileName}) was not found beside Koral's library. Set KORAL_UI_LIBRARY to it.");
    }

    private static IEnumerable<string> Candidates()
    {
        if (Environment.GetEnvironmentVariable("KORAL_UI_LIBRARY") is { Length: > 0 } library) yield return library;
        if (NativeLibraryResolver.LoadedFrom is { } koral && System.IO.Path.GetDirectoryName(System.IO.Path.GetFullPath(koral)) is { } dir)
        {
            yield return System.IO.Path.Combine(dir, "modules", FileName);
            yield return System.IO.Path.Combine(dir, FileName);
        }
        foreach (var candidate in NativeLibraryResolver.Candidates())
            if (System.IO.Path.GetDirectoryName(System.IO.Path.GetFullPath(candidate)) is { } d) yield return System.IO.Path.Combine(d, "modules", FileName);
    }

    /// <summary>Throws what the last call on this thread failed with, if it did: koral_last_error, which the module reports into.</summary>
    public static void Check() => KoralNative.Check();

    public static IntPtr Check(IntPtr handle, [CallerArgumentExpression(nameof(handle))] string? call = null) => KoralNative.Check(handle, call);

    public static byte Bool(bool value) => value ? (byte)1 : (byte)0;
}
