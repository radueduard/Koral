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
            if (File.Exists(candidate) && NativeLibrary.TryLoad(candidate, out var handle) && IsKoral(handle))
            {
                LoadedFrom = candidate;
                return handle;
            }
        }
        if (NativeLibrary.TryLoad(name, assembly, searchPath, out var fromSystem) && IsKoral(fromSystem))
        {
            LoadedFrom = name;
            return fromSystem;
        }
        throw new DllNotFoundException(
            $"Koral's native library ({FileName}) was not found. Set KORAL_SDK to a Koral SDK, or KORAL_LIBRARY to the library itself.");
    }

    // On Windows the managed assembly is Koral.dll too, and sits beside the application: Windows loads
    // an IL-only DLL without complaint, so a file that loads is not yet the engine. One that exports
    // the C interface is.
    private static bool IsKoral(IntPtr handle)
    {
        if (NativeLibrary.TryGetExport(handle, "koral_last_error", out _)) return true;
        NativeLibrary.Free(handle);
        return false;
    }

    /// <summary>
    /// Loads a native module that links against Koral's library — koral-ui, say — so that it binds to the
    /// library C# has loaded.
    /// </summary>
    /// <remarks>
    /// On Windows a module's import of <c>Koral.dll</c> is resolved by name, and the first thing loaded under that
    /// name is the managed assembly: .NET loads its assemblies as Windows loads any DLL. The module is therefore
    /// loaded with the manifest beside the engine in force (<c>Koral.native.manifest</c>, which the build puts
    /// there), which says that <c>Koral.dll</c> is the file beside it. A module with a manifest of its own is
    /// resolved by that instead: Koral's modules are linked without one.
    /// </remarks>
    public static IntPtr LoadBesideKoral(string path)
    {
        unsafe { _ = KoralNative.koral_last_error(); }     // the engine first: it is what the module is to find
        if (!OperatingSystem.IsWindows() || LoadedFrom is null || !File.Exists(LoadedFrom)) return NativeLibrary.Load(path);
        var context = EngineContext();
        if (context == IntPtr.Zero || !ActivateActCtx(context, out var cookie)) return NativeLibrary.Load(path);
        try { return NativeLibrary.Load(path); }
        finally { DeactivateActCtx(0, cookie); }
    }

    private static IntPtr _engineContext;

    private static IntPtr EngineContext()
    {
        if (_engineContext != IntPtr.Zero) return _engineContext;
        var directory = Path.GetDirectoryName(Path.GetFullPath(LoadedFrom!))!;
        var manifest = Path.Combine(directory, "Koral.native.manifest");
        if (!File.Exists(manifest))
        {
            // An engine built before it carried one: written now, where that can be done.
            try
            {
                File.WriteAllText(manifest, """
                    <?xml version="1.0" encoding="UTF-8" standalone="yes"?>
                    <assembly xmlns="urn:schemas-microsoft-com:asm.v1" manifestVersion="1.0">
                      <assemblyIdentity type="win32" name="Koral.Native" version="1.0.0.0"/>
                      <file name="Koral.dll"/>
                    </assembly>
                    """);
            }
            catch (Exception e) when (e is IOException or UnauthorizedAccessException) { return IntPtr.Zero; }
        }
        var description = new ACTCTX { cbSize = Marshal.SizeOf<ACTCTX>(), lpSource = manifest };
        var made = CreateActCtxW(ref description);
        return _engineContext = made == new IntPtr(-1) ? IntPtr.Zero : made;
    }

    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    private struct ACTCTX
    {
        public int cbSize;
        public uint dwFlags;
        public string lpSource;
        public ushort wProcessorArchitecture;
        public ushort wLangId;
        public IntPtr lpAssemblyDirectory;
        public IntPtr lpResourceName;
        public IntPtr lpApplicationName;
        public IntPtr hModule;
    }

    [DllImport("kernel32.dll", CharSet = CharSet.Unicode)] private static extern IntPtr CreateActCtxW(ref ACTCTX context);
    [DllImport("kernel32.dll")] [return: MarshalAs(UnmanagedType.Bool)] private static extern bool ActivateActCtx(IntPtr context, out IntPtr cookie);
    [DllImport("kernel32.dll")] [return: MarshalAs(UnmanagedType.Bool)] private static extern bool DeactivateActCtx(uint flags, IntPtr cookie);

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
