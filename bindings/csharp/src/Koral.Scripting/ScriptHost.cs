using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.Loader;
using System.Text;
using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.CSharp;
using Microsoft.CodeAnalysis.Emit;
using Microsoft.CodeAnalysis.Text;

namespace Koral.Scripting;

/// <summary>A script build that did not compile, with what the compiler said.</summary>
public sealed class ScriptException(string message, IReadOnlyList<string> errors) : Exception(message)
{
    public IReadOnlyList<string> Errors { get; } = errors;
}

/// <summary>
/// A project's scenes, from C# source or a built assembly — registered with the application, and
/// reloaded while they run.
/// </summary>
/// <remarks>
/// <para>
/// Point it at a directory and every <c>.cs</c> file under it (but <c>bin</c> and <c>obj</c>) is
/// compiled together, with <c>System</c>, <c>System.Numerics</c>, <c>System.Linq</c>,
/// <c>System.Collections.Generic</c> and <c>Koral</c> already in scope (and <c>Buffer</c> meaning Koral's). Point it at a <c>.dll</c> — a
/// class library built with <c>dotnet build</c> — and that is loaded instead. Either way, every public
/// concrete <see cref="Scene"/> in it is registered, by its <see cref="SceneAttribute"/> name or its
/// class name.
/// </para>
/// <para>
/// <see cref="Poll"/>, between frames, notices when the source (or the assembly) changed and has
/// stopped changing, builds it again, and reopens every scene from it with its <see cref="KeepAttribute"/>
/// state. A build that fails leaves the running scenes as they are and logs the errors. Each build lives
/// in a collectible load context, unloaded once nothing refers to it, so reloading all day does not
/// grow the process.
/// </para>
/// </remarks>
public sealed class ScriptHost : IDisposable
{
    private static readonly string[] GlobalUsings =
        ["System", "System.Collections.Generic", "System.Linq", "System.Numerics", "Koral"];

    private readonly App _app;
    private ScriptContext? _context;
    private List<string> _scenes = new();
    private int _generation;
    private string _snapshot = "";
    private string? _pending;
    private DateTime _pendingSince;
    private DateTime _lastPoll;

    /// <param name="app">The application to register the scenes with.</param>
    /// <param name="source">A directory of <c>.cs</c> files, or a <c>.dll</c>.</param>
    public ScriptHost(App app, string source)
    {
        _app = app;
        Source = Path.GetFullPath(source);
        if (!Directory.Exists(Source) && !File.Exists(Source))
            throw new FileNotFoundException($"'{source}' is neither a directory of scripts nor an assembly.", source);
    }

    /// <summary>What the scenes are built from: a directory of scripts, or an assembly.</summary>
    public string Source { get; }

    public bool IsAssembly => File.Exists(Source);

    /// <summary>The scenes the current build registered.</summary>
    public IReadOnlyList<string> Scenes => _scenes;

    /// <summary>How many builds have been loaded.</summary>
    public int Generation => _generation;

    /// <summary>How long the source must stay unchanged before a change is built: an editor or a compiler still writing is not caught halfway.</summary>
    public TimeSpan SettleTime { get; set; } = TimeSpan.FromMilliseconds(300);

    /// <summary>How often <see cref="Poll"/> looks at the files.</summary>
    public TimeSpan PollInterval { get; set; } = TimeSpan.FromMilliseconds(250);

    /// <summary>A build finished loading: its scene names.</summary>
    public event Action<IReadOnlyList<string>>? Reloaded;

    /// <summary>A build failed: what the compiler said.</summary>
    public event Action<IReadOnlyList<string>>? BuildFailed;

    /// <summary>The last unloaded build's context, for as long as something still holds it. For tests and diagnostics.</summary>
    public WeakReference? PreviousContext { get; private set; }

    /// <summary>Builds and registers the scenes. Throws <see cref="ScriptException"/> when it does not compile.</summary>
    public void Load()
    {
        _snapshot = Snapshot();
        var (context, types) = Build();
        Install(context, types, reload: false);
    }

    /// <summary>
    /// Between frames: reloads when the source changed and has settled. True when it reloaded. Cheap to
    /// call every frame — it only looks at the files every <see cref="PollInterval"/>.
    /// </summary>
    public bool Poll()
    {
        var now = DateTime.UtcNow;
        if (now - _lastPoll < PollInterval) return false;
        _lastPoll = now;
        var snapshot = Snapshot();
        if (snapshot == _snapshot) { _pending = null; return false; }
        if (snapshot != _pending) { _pending = snapshot; _pendingSince = now; return false; }
        if (now - _pendingSince < SettleTime) return false;
        _snapshot = snapshot;
        _pending = null;
        return Reload();
    }

    /// <summary>Builds again now, and reopens the scenes from it. False (with the errors logged) when it does not compile.</summary>
    public bool Reload()
    {
        (ScriptContext Context, List<Type> Types) built;
        try
        {
            built = Build();
        }
        catch (ScriptException e)
        {
            Log.Error($"[scripts] the change did not compile; the scenes keep running as they were.\n{string.Join('\n', e.Errors)}");
            BuildFailed?.Invoke(e.Errors);
            return false;
        }
        catch (Exception e) when (e is IOException or BadImageFormatException or ReflectionTypeLoadException)
        {
            Log.Error($"[scripts] the new build could not be loaded: {e.Message}");
            BuildFailed?.Invoke([e.Message]);
            return false;
        }
        Install(built.Context, built.Types, reload: true);
        return true;
    }

    private void Install(ScriptContext context, List<Type> types, bool reload)
    {
        var names = new List<string>();
        foreach (var type in types)
        {
            var name = App.SceneNameOf(type);
            _app.Register(type, name);
            names.Add(name);
        }

        var previous = _context;
        _context = context;
        _scenes = names;
        ++_generation;

        if (reload)
        {
            // A scene whose class is gone keeps running the code it was opened with, until it closes.
            try
            {
                _app.ReloadScenes(names);
                Log.Info($"[scripts] reloaded: {string.Join(", ", names)}");
            }
            catch (KoralException e)
            {
                Log.Error($"[scripts] the scenes could not be reopened: {e.Message}");
            }
        }

        if (previous is not null) Unload(previous);
        Reloaded?.Invoke(names);
    }

    [MethodImpl(MethodImplOptions.NoInlining)]
    private void Unload(ScriptContext context)
    {
        PreviousContext = new WeakReference(context);
        context.Unload();
    }

    // ---- building ----------------------------------------------------------------------------------

    private (ScriptContext, List<Type>) Build()
    {
        var generation = _generation + 1;
        var context = new ScriptContext($"Koral.Scripts.{generation}", IsAssembly ? Path.GetDirectoryName(Source)! : Source);
        Assembly assembly;
        try
        {
            assembly = IsAssembly ? LoadAssembly(context) : Compile(context, generation);
        }
        catch
        {
            context.Unload();
            throw;
        }
        var types = assembly.GetExportedTypes()
            .Where(t => t.IsClass && !t.IsAbstract && t.IsSubclassOf(typeof(Scene)))
            .OrderBy(t => t.FullName, StringComparer.Ordinal)
            .ToList();
        return (context, types);
    }

    private Assembly LoadAssembly(ScriptContext context)
    {
        // From bytes, not the file: a loaded file is locked on Windows, and the next build could not replace it.
        var image = File.ReadAllBytes(Source);
        var pdbPath = Path.ChangeExtension(Source, ".pdb");
        using var pe = new MemoryStream(image);
        using var pdb = File.Exists(pdbPath) ? new MemoryStream(File.ReadAllBytes(pdbPath)) : null;
        return context.LoadFromStream(pe, pdb);
    }

    private Assembly Compile(ScriptContext context, int generation)
    {
        var parse = new CSharpParseOptions(LanguageVersion.Latest);
        var trees = new List<SyntaxTree>
        {
            CSharpSyntaxTree.ParseText(string.Concat(GlobalUsings.Select(u => $"global using {u};\n"))
                // Koral.Buffer, not System.Buffer: both namespaces are imported, and the alias decides.
                + "global using Buffer = Koral.Buffer;\n", parse, "GlobalUsings.g.cs", Encoding.UTF8),
        };
        var embedded = new List<EmbeddedText>();
        foreach (var file in SourceFiles())
        {
            var bytes = File.ReadAllBytes(file);
            var text = SourceText.From(bytes, bytes.Length, Encoding.UTF8, canBeEmbedded: true);
            trees.Add(CSharpSyntaxTree.ParseText(text, parse, file));
            embedded.Add(EmbeddedText.FromSource(file, text));
        }
        if (trees.Count == 1) throw new ScriptException($"'{Source}' has no .cs files.", [$"'{Source}' has no .cs files."]);

        var compilation = CSharpCompilation.Create(
            $"Koral.Scripts.{generation}",
            trees,
            References(),
            new CSharpCompilationOptions(OutputKind.DynamicallyLinkedLibrary,
                nullableContextOptions: NullableContextOptions.Enable,
                optimizationLevel: OptimizationLevel.Debug,
                allowUnsafe: true));

        using var pe = new MemoryStream();
        using var pdb = new MemoryStream();
        var result = compilation.Emit(pe, pdb,
            embeddedTexts: embedded,
            options: new EmitOptions(debugInformationFormat: DebugInformationFormat.PortablePdb));
        if (!result.Success)
        {
            var errors = result.Diagnostics
                .Where(d => d.Severity == DiagnosticSeverity.Error)
                .Select(d => d.ToString())
                .ToList();
            throw new ScriptException($"The scripts in '{Source}' did not compile.", errors);
        }
        pe.Position = 0;
        pdb.Position = 0;
        return context.LoadFromStream(pe, pdb);
    }

    private IEnumerable<string> SourceFiles() =>
        Directory.EnumerateFiles(Source, "*.cs", SearchOption.AllDirectories)
            .Where(f => !IsBuildOutput(Path.GetRelativePath(Source, f)))
            .OrderBy(f => f, StringComparer.Ordinal);

    private static bool IsBuildOutput(string relative) =>
        relative.Split(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar)
            .Any(part => part is "bin" or "obj");

    private IEnumerable<MetadataReference> References()
    {
        // The framework this process runs on, Koral, and any library dropped beside the scripts.
        var paths = ((string?)AppContext.GetData("TRUSTED_PLATFORM_ASSEMBLIES") ?? "")
            .Split(Path.PathSeparator, StringSplitOptions.RemoveEmptyEntries)
            .ToHashSet(StringComparer.OrdinalIgnoreCase);
        paths.Add(typeof(Scene).Assembly.Location);
        foreach (var library in Directory.EnumerateFiles(Source, "*.dll", SearchOption.TopDirectoryOnly)) paths.Add(library);
        return paths.Where(p => p.Length > 0 && File.Exists(p)).Select(p => MetadataReference.CreateFromFile(p));
    }

    // Everything a build of the directory depends on, as one string: a change to any file changes it.
    private string Snapshot()
    {
        var builder = new StringBuilder();
        IEnumerable<string> files = IsAssembly ? [Source] : SourceFiles();
        foreach (var file in files)
        {
            try
            {
                var info = new FileInfo(file);
                builder.Append(file).Append('|').Append(info.LastWriteTimeUtc.Ticks).Append('|').Append(info.Length).Append('\n');
            }
            catch (IOException)
            {
                builder.Append(file).Append("|?\n");
            }
        }
        return builder.ToString();
    }

    public void Dispose()
    {
        // The scenes from the last build may still be open; their context goes when the process does.
        _context = null;
    }

    /// <summary>
    /// One build's assemblies. Koral itself, and everything the application already has, come from the
    /// default context — so a script's Scene is the runtime's Scene — and a library beside the scripts
    /// is loaded into this one, to be unloaded with it.
    /// </summary>
    private sealed class ScriptContext(string name, string directory) : AssemblyLoadContext(name, isCollectible: true)
    {
        protected override Assembly? Load(AssemblyName assemblyName)
        {
            if (Default.Assemblies.Any(a => AssemblyName.ReferenceMatchesDefinition(a.GetName(), assemblyName)))
                return null;
            var candidate = Path.Combine(directory, assemblyName.Name + ".dll");
            if (!File.Exists(candidate)) return null;
            using var stream = new MemoryStream(File.ReadAllBytes(candidate));
            return LoadFromStream(stream);
        }
    }
}
