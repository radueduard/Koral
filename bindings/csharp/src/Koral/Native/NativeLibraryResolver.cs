using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace Koral.Native;

/// <summary>
/// Finds the native Koral library, on whichever OS this is. In order:
/// <list type="number">
/// <item><c>KORAL_LIBRARY</c>: the library file itself.</item>
/// <item><c>KORAL_SDK</c>: an SDK's <c>bin</c> (Windows) or <c>lib</c> (Linux, macOS).</item>
/// <item>Beside the application, and where an SDK installs koral-dotnet relative to it
/// (<c>lib/koral-dotnet</c>, with the library in <c>lib</c> — or <c>bin</c>, on Windows).</item>
/// <item>Wherever the OS looks: <c>PATH</c>, <c>LD_LIBRARY_PATH</c>, <c>DYLD_LIBRARY_PATH</c>.</item>
/// </list>
/// </summary>
public static class NativeLibraryResolver
{
    /// <summary>The file the native library is, on this OS.</summary>
    public static string FileName =>
        OperatingSystem.IsWindows() ? "Koral.dll" :
        OperatingSystem.IsMacOS() ? "libKoral.dylib" :
        "libKoral.so";

    /// <summary>Where the library was loaded from, once it has been.</summary>
    public static string? LoadedFrom { get; private set; }

    // Before any P/Invoke in this assembly: the one thing a library's module initialiser is for.
#pragma warning disable CA2255
    [ModuleInitializer]
#pragma warning restore CA2255
    internal static void Register() =>
        NativeLibrary.SetDllImportResolver(typeof(NativeLibraryResolver).Assembly, Resolve);

    private static IntPtr Resolve(string name, Assembly assembly, DllImportSearchPath? searchPath)
    {
        if (name != KoralNative.Library) return IntPtr.Zero;
        foreach (var candidate in Candidates())
        {
            if (File.Exists(candidate) && NativeLibrary.TryLoad(candidate, out var handle))
            {
                LoadedFrom = candidate;
                return handle;
            }
        }
        if (NativeLibrary.TryLoad(name, assembly, searchPath, out var fromSystem))
        {
            LoadedFrom = name;
            return fromSystem;
        }
        throw new DllNotFoundException(
            $"Koral's native library ({FileName}) was not found. Set KORAL_SDK to a Koral SDK, or KORAL_LIBRARY to the library itself.");
    }

    /// <summary>Every path tried before the OS's own search, in order.</summary>
    public static IEnumerable<string> Candidates()
    {
        if (Environment.GetEnvironmentVariable("KORAL_LIBRARY") is { Length: > 0 } library)
            yield return library;
        if (Environment.GetEnvironmentVariable("KORAL_SDK") is { Length: > 0 } sdk)
        {
            if (OperatingSystem.IsWindows()) yield return Path.Combine(sdk, "bin", FileName);
            yield return Path.Combine(sdk, "lib", FileName);
            yield return Path.Combine(sdk, "lib64", FileName);
        }
        var here = AppContext.BaseDirectory;
        yield return Path.Combine(here, FileName);
        yield return Path.GetFullPath(Path.Combine(here, "..", FileName));
        if (OperatingSystem.IsWindows()) yield return Path.GetFullPath(Path.Combine(here, "..", "..", "bin", FileName));
    }
}
