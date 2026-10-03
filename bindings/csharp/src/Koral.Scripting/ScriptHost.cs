using System.Reflection;
using System.Reflection.Metadata;
using System.Runtime.CompilerServices;
using System.Runtime.Loader;
using System.Text;
using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.CSharp;
using Microsoft.CodeAnalysis.CSharp.Syntax;
using Microsoft.CodeAnalysis.Emit;
using Microsoft.CodeAnalysis.Text;

namespace Koral.Scripting;

/// <summary>A script build that did not compile, with what the compiler said.</summary>
public sealed class ScriptException(string message, IReadOnlyList<string> errors) : Exception(message)
{
    public IReadOnlyList<string> Errors { get; } = errors;
}

/// <summary>What the last <see cref="ScriptHost.Reload"/> did.</summary>
public enum ReloadKind
{
    /// <summary>Nothing had changed.</summary>
    None,
    /// <summary>
    /// The edited methods were replaced in the running code: every scene, pass and window carries on, and
    /// the next call runs the new code. Passes whose Setup or Initialize changed are set up again.
    /// </summary>
    InPlace,
    /// <summary>Updated in place, and the scenes whose constructor or Initialize changed opened again — in their windows, with their state.</summary>
    ScenesReopened,
    /// <summary>A new build: every scene from the scripts opened again, in its window, with its state.</summary>
    Full,
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
/// stopped changing, and applies it. An edit inside method bodies — what most edits are — is applied to
/// the running code in place, as .NET Hot Reload does: nothing is reopened, and the next frame runs the new
/// code. A pass whose Setup or Initialize changed is set up again; a scene whose constructor or Initialize
/// changed is opened again, in its window, with its <see cref="KeepAttribute"/> state. Anything else — a
/// field or a class added, a signature changed — is a new build, every scene from it reopened in its window
/// with its state. A build that fails leaves the running scenes as they are and logs the errors.
/// </para>
/// <para>
/// In-place updates need the process started with <c>DOTNET_MODIFIABLE_ASSEMBLIES=debug</c> (koral-dotnet
/// sees to it). Without it every change is a new build, and each build lives in a collectible load
/// context, unloaded once nothing refers to it.
/// </para>
/// </remarks>
public sealed class ScriptHost : IDisposable
{
    private static readonly string[] GlobalUsings =
        ["System", "System.Collections.Generic", "System.Linq", "System.Numerics", "Koral"];

    private readonly App _app;
    private readonly bool _inPlace;
    private ScriptContext? _context;
    private Assembly? _assembly;
    private CSharpCompilation? _compilation;
    private EmitBaseline? _baseline;
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
        _inPlace = MetadataUpdater.IsSupported && !IsAssembly;
    }

    /// <summary>Whether edits are applied to the running code (the process allows it, and the scenes are scripts).</summary>
    public bool UpdatesInPlace => _inPlace;

    /// <summary>What the last reload did.</summary>
    public ReloadKind LastReload { get; private set; }

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

    /// <summary>
    /// Applies the scripts as they are now: in place when it can be, as a new build when not. False (with
    /// the errors logged) when they do not compile.
    /// </summary>
    public bool Reload()
    {
        if (_inPlace && _baseline is not null && !UnderDebugger() && TryUpdateInPlace(out var updated)) return updated;
        LastReload = ReloadKind.Full;
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

        if (previous is not null && previous.IsCollectible) Unload(previous);
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
        var context = new ScriptContext($"Koral.Scripts.{generation}", IsAssembly ? Path.GetDirectoryName(Source)! : Source,
                                        collectible: !_inPlace);
        Assembly assembly;
        try
        {
            assembly = IsAssembly ? LoadAssembly(context) : Compile(context, generation);
        }
        catch
        {
            if (context.IsCollectible) context.Unload();
            throw;
        }
        _assembly = assembly;
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
                + "global using Buffer = Koral.Buffer;\n", parse, GlobalUsingsFile, Encoding.UTF8),
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

        var compilation = _compilation = CSharpCompilation.Create(
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
        if (_inPlace)
        {
            // What later edits are compiled against: their deltas are to this build.
            _baseline = EmitBaseline.CreateInitialBaseline(compilation, ModuleMetadata.CreateFromImage(pe.ToArray()),
                _ => default, _ => default, hasPortableDebugInformation: true);
        }
        // Loaded from a file with its .pdb beside it, not from memory: that is what every debugger finds
        // symbols for, so a breakpoint in a script binds. One folder per process, gone with it (see Sweep).
        var directory = BuildDirectory();
        var dll = Path.Combine(directory, $"{compilation.AssemblyName}.dll");
        File.WriteAllBytes(dll, pe.ToArray());
        File.WriteAllBytes(Path.ChangeExtension(dll, ".pdb"), pdb.ToArray());
        return context.LoadFromAssemblyPath(dll);
    }

    private static string? s_buildDirectory;

    /// <summary>Where this process writes its script builds: a folder of its own, the ones of processes gone swept away.</summary>
    private static string BuildDirectory()
    {
        if (s_buildDirectory is not null) return s_buildDirectory;
        var root = Path.Combine(Path.GetTempPath(), "koral-scripts");
        Sweep(root);
        s_buildDirectory = Path.Combine(root, Environment.ProcessId.ToString());
        Directory.CreateDirectory(s_buildDirectory);
        return s_buildDirectory;
    }

    private static void Sweep(string root)
    {
        if (!Directory.Exists(root)) return;
        foreach (var dir in Directory.EnumerateDirectories(root))
        {
            if (int.TryParse(Path.GetFileName(dir), out var pid) && Running(pid)) continue;
            try { Directory.Delete(dir, recursive: true); } catch (Exception e) when (e is IOException or UnauthorizedAccessException) { }
        }
    }

    private static bool Running(int pid)
    {
        try
        {
            using var process = System.Diagnostics.Process.GetProcessById(pid);
            return !process.HasExited;
        }
        catch (ArgumentException)
        {
            return false;
        }
    }

    // ---- updating in place ------------------------------------------------------------------------

    /// <summary>
    /// Applies the edit to the running code, when it touches nothing but method bodies. False when it
    /// cannot be (the caller makes a new build instead); <paramref name="succeeded"/> is false when it does
    /// not compile.
    /// </summary>
    private bool TryUpdateInPlace(out bool succeeded)
    {
        succeeded = false;
        var old = _compilation!;
        var parse = (CSharpParseOptions)old.SyntaxTrees.First().Options;
        var trees = old.SyntaxTrees.Where(t => t.FilePath != GlobalUsingsFile).ToDictionary(t => t.FilePath);
        var files = SourceFiles().ToList();
        if (!files.ToHashSet().SetEquals(trees.Keys)) return false;   // a file added or removed: a new build

        var compilation = old;
        var changed = new List<(SyntaxTree Old, SyntaxTree New)>();
        foreach (var file in files)
        {
            var bytes = File.ReadAllBytes(file);
            var text = SourceText.From(bytes, bytes.Length, Encoding.UTF8, canBeEmbedded: true);
            if (text.ContentEquals(trees[file].GetText())) continue;
            var tree = CSharpSyntaxTree.ParseText(text, parse, file);
            compilation = compilation.ReplaceSyntaxTree(trees[file], tree);
            changed.Add((trees[file], tree));
        }
        if (changed.Count == 0)
        {
            LastReload = ReloadKind.None;
            succeeded = true;
            return true;
        }

        var errors = compilation.GetDiagnostics().Where(d => d.Severity == DiagnosticSeverity.Error).Select(d => d.ToString()).ToList();
        if (errors.Count > 0)
        {
            Log.Error($"[scripts] the change did not compile; the scenes keep running as they were.\n{string.Join('\n', errors)}");
            BuildFailed?.Invoke(errors);
            return true;
        }

        // Anything but method bodies — a field, a signature, a class, an initializer — changes what the
        // running code is made of, and only a new build can carry that.
        if (changed.Any(c => Shape(c.Old) != Shape(c.New))) return false;

        var edits = new List<SemanticEdit>();
        var edited = new List<IMethodSymbol>();
        foreach (var (oldTree, newTree) in changed)
        {
            var before = Bodies(old.GetSemanticModel(oldTree));
            foreach (var (id, (method, text, line)) in Bodies(compilation.GetSemanticModel(newTree)))
            {
                if (!before.TryGetValue(id, out var was)) return false;
                // Moved counts too: its lines changed, and the debugger's map of them must follow.
                if (was.Text == text && was.Line == line) continue;
                edits.Add(new SemanticEdit(SemanticEditKind.Update, was.Method, method));
                edited.Add(method);
            }
        }

        using var metadata = new MemoryStream();
        using var il = new MemoryStream();
        using var pdb = new MemoryStream();
        var delta = compilation.EmitDifference(_baseline!, edits, _ => false, metadata, il, pdb);
        if (!delta.Success)
        {
            Log.Info($"[scripts] this edit cannot be applied in place, so it is a new build: {delta.Diagnostics.FirstOrDefault()}");
            return false;
        }
        try
        {
            MetadataUpdater.ApplyUpdate(_assembly!, metadata.ToArray(), il.ToArray(), pdb.ToArray());
        }
        catch (Exception e) when (e is InvalidOperationException or NotSupportedException)
        {
            Log.Info($"[scripts] the runtime refused the update ({e.Message}), so it is a new build");
            return false;
        }
        _compilation = compilation;
        _baseline = delta.Baseline;
        succeeded = true;
        Rerun(edited);
        return true;
    }

    private bool _toldAboutDebugger;

    // The runtime refuses to change running code behind a debugger's back: under one, changes go through
    // the debugger's own Edit and Continue, which only reaches projects the IDE builds. So a debugged
    // session reopens the edited scenes instead — in their windows, with their state, breakpoints rebound.
    private bool UnderDebugger()
    {
        if (!System.Diagnostics.Debugger.IsAttached) return false;
        if (!_toldAboutDebugger)
        {
            Log.Info("[scripts] a debugger is attached, which the runtime does not let code be replaced under: "
                     + "edits reopen their scenes (in their windows, with their state). Run without debugging to apply them in place.");
            _toldAboutDebugger = true;
        }
        return true;
    }

    /// <summary>What an updated method means for what is running: which passes to set up again, which scenes to reopen.</summary>
    private void Rerun(List<IMethodSymbol> edited)
    {
        var reopen = new HashSet<string>();
        var reopenAll = false;
        var passes = new HashSet<Type>();
        foreach (var method in edited)
        {
            var type = _assembly!.GetType(MetadataName(method.ContainingType));
            if (type is null) continue;
            var constructs = method.MethodKind is MethodKind.Constructor or MethodKind.StaticConstructor;
            if (type.IsSubclassOf(typeof(Scene)))
            {
                if (constructs || method.Name is "Initialize" or "State") reopen.Add(App.SceneNameOf(type));
            }
            else if (constructs && !HotReload.IsClaimed(type))
            {
                // Something a scene makes — a pass, a helper — made differently now: the scenes make theirs again.
                // (Unless its library makes it again itself: an interface's widgets are built anew on reload.)
                reopenAll = true;
            }
            else if (type.IsSubclassOf(typeof(RenderPass)) && method.Name is "Setup" or "Initialize")
            {
                passes.Add(type);
            }
        }

        foreach (var pass in passes.SelectMany(RenderPass.Live).Distinct()) pass.Reinitialize();
        // The libraries built on Koral hear of it: an interface builds its widgets again, with the new code.
        HotReload.NotifyUpdated(edited.Select(m => _assembly!.GetType(MetadataName(m.ContainingType))).OfType<Type>().Distinct().ToList());

        var names = reopenAll ? _scenes : _scenes.Where(reopen.Contains).ToList();
        LastReload = names.Count > 0 ? ReloadKind.ScenesReopened : ReloadKind.InPlace;
        var what = string.Join(", ", edited.Select(m => $"{m.ContainingType.Name}.{(m.MethodKind == MethodKind.Constructor ? "ctor" : m.Name)}").Distinct());
        if (names.Count > 0)
        {
            try
            {
                _app.ReloadScenes(names);
            }
            catch (KoralException e)
            {
                Log.Error($"[scripts] the scenes could not be reopened: {e.Message}");
            }
            Log.Info($"[scripts] updated in place: {what}; reopened {string.Join(", ", names)}");
        }
        else
        {
            Log.Info($"[scripts] updated in place: {what}");
        }
        Reloaded?.Invoke(_scenes);
    }

    private const string GlobalUsingsFile = "GlobalUsings.g.cs";

    private static string MetadataName(INamedTypeSymbol type)
    {
        var name = type.MetadataName;
        for (var outer = type.ContainingType; outer is not null; outer = outer.ContainingType) name = outer.MetadataName + "+" + name;
        var ns = type.ContainingNamespace;
        return ns is null || ns.IsGlobalNamespace ? name : ns.ToDisplayString() + "." + name;
    }

    /// <summary>
    /// A file without its method bodies, as tokens: equal before and after an edit exactly when the edit
    /// changed nothing but bodies.
    /// </summary>
    private static string Shape(SyntaxTree tree)
    {
        var tokens = tree.GetRoot().DescendantTokens(node => !IsBody(node));
        return string.Join(" ", tokens.Select(t => t.Text));
    }

    private static bool IsBody(SyntaxNode node) => node.Parent switch
    {
        BaseMethodDeclarationSyntax m => node == m.Body || node == m.ExpressionBody,
        AccessorDeclarationSyntax a => node == a.Body || node == a.ExpressionBody,
        PropertyDeclarationSyntax p => node == p.ExpressionBody,
        IndexerDeclarationSyntax i => node == i.ExpressionBody,
        _ => false,
    };

    /// <summary>Every method with a body in a file: by its documentation ID, with its text and where it starts.</summary>
    private static Dictionary<string, (IMethodSymbol Method, string Text, int Line)> Bodies(SemanticModel model)
    {
        var bodies = new Dictionary<string, (IMethodSymbol, string, int)>();
        foreach (var node in model.SyntaxTree.GetRoot().DescendantNodes())
        {
            var method = node switch
            {
                BaseMethodDeclarationSyntax m when m.Body is not null || m.ExpressionBody is not null => model.GetDeclaredSymbol(m) as IMethodSymbol,
                AccessorDeclarationSyntax a when a.Body is not null || a.ExpressionBody is not null => model.GetDeclaredSymbol(a) as IMethodSymbol,
                PropertyDeclarationSyntax { ExpressionBody: not null } p => model.GetDeclaredSymbol(p)?.GetMethod,
                IndexerDeclarationSyntax { ExpressionBody: not null } i => model.GetDeclaredSymbol(i)?.GetMethod,
                _ => null,
            };
            if (method?.GetDocumentationCommentId() is not { } id) continue;
            var text = string.Join(" ", node.DescendantTokens().Select(t => t.Text));
            bodies[id] = (method, text, node.GetLocation().GetLineSpan().StartLinePosition.Line);
        }
        return bodies;
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
    private sealed class ScriptContext(string name, string directory, bool collectible = true)
        : AssemblyLoadContext(name, isCollectible: collectible)
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
