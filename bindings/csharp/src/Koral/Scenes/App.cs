using System.Diagnostics.CodeAnalysis;
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using Koral.Native;

namespace Koral;

/// <summary>
/// kor::App: the device, the frames, and the scenes they run. One per process, used from the thread that
/// made it.
/// </summary>
/// <example>
/// <code>
/// using var app = new App();
/// app.Register&lt;Menu&gt;("Menu");
/// app.Register&lt;Level&gt;("Level");
/// app.Open("Menu", new WindowSettings { Title = "My Game" });
/// return app.Run();
/// </code>
/// </example>
public sealed unsafe class App : IDisposable, IResourceOwner
{
    private readonly List<IDisposable> _owned = [];
    private readonly Dictionary<string, GCHandle> _factories = new();
    private readonly Dictionary<string, WeakReference> _shared = new();
    private readonly SynchronizationContext? _previousContext;
    private bool _disposed;

    /// <summary>App::Current(): the application, while there is one.</summary>
    public static App? Current { get; private set; }

    /// <summary>App::Exists().</summary>
    public static bool Exists => Current is not null;

    /// <summary>
    /// A C# scene's hook threw. The exception is logged either way; this is for a program (or a test) that
    /// wants to do more about it. Raised on the thread the hook ran on.
    /// </summary>
    public static event Action<Scene, string, Exception>? HookFailed;

    public App(AppSettings? settings = null)
    {
        if (Current is not null) throw new InvalidOperationException("There is already an application: one per process.");
        Settings = settings ?? new AppSettings();
        var gpu = KoralNative.Utf8(Settings.Gpu);
        var interfaceDirectory = KoralNative.Utf8(Settings.InterfaceDirectory);
        try
        {
            var native = new KoralAppSettings
            {
                api = (uint)Settings.Api,
                platform = (int)Settings.Platform,
                frames_in_flight = Settings.FramesInFlight,
                gpu = gpu,
                interface_directory = interfaceDirectory,
            };
            KoralNative.Check(KoralNative.koral_app_create(&native));
        }
        finally
        {
            KoralNative.Free(gpu);
            KoralNative.Free(interfaceDirectory);
        }
        Current = this;
        // Awaits outside any scene come back here too, at the start of a frame.
        _previousContext = SynchronizationContext.Current;
        SynchronizationContext.SetSynchronizationContext(SceneSynchronizationContext.None);
    }

    public AppSettings Settings { get; }

    // ---- the registry ---------------------------------------------------------------------------------

    /// <summary>Register(name, factory): opening <paramref name="name"/> runs <paramref name="factory"/>. Registering again replaces it.</summary>
    public void Register(string name, Func<SceneArgs, Scene> factory)
    {
        ThrowIfDisposed();
        var handle = GCHandle.Alloc(factory);
        var status = KoralNative.koral_app_register(name, &MakeScene, (void*)GCHandle.ToIntPtr(handle));
        if (status != 0)
        {
            handle.Free();
            KoralNative.Check(status);
        }
        if (_factories.Remove(name, out var previous)) previous.Free();
        _factories[name] = handle;
    }

    /// <summary>
    /// Register&lt;S&gt;(name): opening <paramref name="name"/> makes an <typeparamref name="S"/> — with its
    /// SceneArgs constructor when it has one, its parameterless one otherwise. The name defaults to the
    /// class's (or its <see cref="SceneAttribute"/>'s).
    /// </summary>
    public void Register<[DynamicallyAccessedMembers(DynamicallyAccessedMemberTypes.PublicConstructors)] S>(string? name = null) where S : Scene =>
        Register(typeof(S), name);

    /// <summary>Register for a type known only at run time — one compiled from a script.</summary>
    public void Register([DynamicallyAccessedMembers(DynamicallyAccessedMemberTypes.PublicConstructors)] Type type, string? name = null)
    {
        if (!type.IsSubclassOf(typeof(Scene)) || type.IsAbstract)
            throw new ArgumentException($"{type} is not a scene: it must be a concrete class deriving from Koral.Scene.");
        var withArgs = type.GetConstructor([typeof(SceneArgs)]);
        var plain = type.GetConstructor(Type.EmptyTypes);
        if (withArgs is null && plain is null)
            throw new ArgumentException($"{type} needs a public constructor taking SceneArgs, or none.");
        Register(name ?? SceneNameOf(type), args =>
        {
            try
            {
                return (Scene)(withArgs is not null ? withArgs.Invoke([args]) : plain!.Invoke(null));
            }
            catch (TargetInvocationException e) when (e.InnerException is not null)
            {
                throw e.InnerException;
            }
        });
    }

    /// <summary>The name a scene class is registered under: its <see cref="SceneAttribute"/>'s, or the class's.</summary>
    public static string SceneNameOf(Type type) => type.GetCustomAttribute<SceneAttribute>()?.Name ?? type.Name;

    /// <summary>LoadLibrary(path): a C++ scene library's scenes, registered. Returns their names.</summary>
    public IReadOnlyList<string> LoadLibrary(string path)
    {
        var before = SceneNames.ToHashSet();
        KoralNative.Check(KoralNative.koral_app_load_library(path));
        return SceneNames.Where(n => !before.Contains(n)).ToList();
    }

    public void UnloadLibrary(string path) => KoralNative.Check(KoralNative.koral_app_unload_library(path));
    public void ReloadLibrary(string path) => KoralNative.Check(KoralNative.koral_app_reload_library(path));

    /// <summary>
    /// ReloadScenes(names): every open scene registered under one of <paramref name="names"/> is opened again
    /// from what is registered under that name now, with its state. Between frames.
    /// </summary>
    public void ReloadScenes(params IReadOnlyCollection<string> names)
    {
        if (names.Count == 0) return;
        try
        {
            KoralNative.Check(KoralNative.koral_app_reload_scenes(names.ToArray(), (nuint)names.Count));
        }
        finally
        {
            // Nothing is opened by a reload's factories: holding the last one here would keep its
            // assembly from being unloaded at the next.
            SceneBridge.LastMade = null;
        }
    }

    /// <summary>SceneNames(): every name that can be opened.</summary>
    public IReadOnlyList<string> SceneNames
    {
        get
        {
            var count = KoralNative.koral_app_scene_name_count();
            var names = new string[count];
            for (uint i = 0; i < count; ++i) names[i] = KoralNative.Text(KoralNative.koral_app_scene_name(i));
            return names;
        }
    }

    // ---- opening --------------------------------------------------------------------------------------

    /// <summary>Open(name, window, arguments): <paramref name="name"/>, in a window of its own.</summary>
    public Scene Open(string name, WindowSettings? window = null, SceneArgs? arguments = null)
    {
        ThrowIfDisposed();
        var json = SceneArgs.JsonOf(arguments);
        return Opened((window ?? new WindowSettings { Title = name }).WithNative(w => KoralNative.koral_app_open(name, (KoralWindowSettings*)w, json)));
    }

    /// <summary>Open(name, scene, window): a scene made here, rather than by a registered factory.</summary>
    public Scene Open(string name, Scene scene, WindowSettings? window = null)
    {
        ThrowIfDisposed();
        var callbacks = stackalloc KoralSceneCallbacks[1];
        callbacks[0] = SceneBridge.CallbacksFor(scene);
        var native = (window ?? new WindowSettings { Title = name }).WithNative(w => KoralNative.koral_app_open_scene(name, callbacks, (KoralWindowSettings*)w));
        SceneBridge.Adopt(scene, KoralNative.Check(native));
        return scene;
    }

    /// <summary>Open&lt;S&gt;(window): a new <typeparamref name="S"/>, named by the window's title.</summary>
    public S Open<S>(WindowSettings window) where S : Scene, new() => (S)Open(window.Title, new S(), window);

    /// <summary>OpenOffscreen(name, target, arguments): <paramref name="name"/>, drawing into an image rather than a window.</summary>
    public Scene OpenOffscreen(string name, OffscreenSettings? target = null, SceneArgs? arguments = null)
    {
        ThrowIfDisposed();
        var json = SceneArgs.JsonOf(arguments);
        return Opened((target ?? new OffscreenSettings { Title = name }).WithNative(t => KoralNative.koral_app_open_offscreen(name, (KoralOffscreenSettings*)t, json)));
    }

    public Scene OpenOffscreen(string name, Scene scene, OffscreenSettings? target = null)
    {
        ThrowIfDisposed();
        var callbacks = stackalloc KoralSceneCallbacks[1];
        callbacks[0] = SceneBridge.CallbacksFor(scene);
        var native = (target ?? new OffscreenSettings { Title = name }).WithNative(t => KoralNative.koral_app_open_offscreen_scene(name, callbacks, (KoralOffscreenSettings*)t));
        SceneBridge.Adopt(scene, KoralNative.Check(native));
        return scene;
    }

    public S OpenOffscreen<S>(OffscreenSettings target) where S : Scene, new() => (S)OpenOffscreen(target.Title, new S(), target);

    internal static Scene Opened(IntPtr native)
    {
        KoralNative.Check(native);
        var made = SceneBridge.LastMade;
        SceneBridge.LastMade = null;
        if (made is not null)
        {
            SceneBridge.Adopt(made, native);
            return made;
        }
        return SceneBridge.Of(native)!;
    }

    // ---- sharing --------------------------------------------------------------------------------------

    /// <summary>
    /// Shared&lt;T&gt;(key, ...): the object shared under <paramref name="key"/>, made by <paramref name="make"/>
    /// the first time — alive for as long as anyone holds it.
    /// </summary>
    public T Shared<T>(string key, Func<T> make) where T : class
    {
        if (_shared.TryGetValue(key, out var weak) && weak.Target is { } existing)
            return existing as T ?? throw new InvalidOperationException($"'{key}' is shared as a {existing.GetType().Name}, not a {typeof(T).Name}");
        var made = make();
        _shared[key] = new WeakReference(made);
        return made;
    }

    public T Shared<T>(string key) where T : class, new() => Shared(key, () => new T());

    public bool IsShared(string key) => _shared.TryGetValue(key, out var weak) && weak.IsAlive;

    // ---- running --------------------------------------------------------------------------------------

    /// <summary>Scenes(): the scene each window shows, in the order the windows were opened.</summary>
    public IReadOnlyList<Scene> Scenes
    {
        get
        {
            var count = (int)KoralNative.koral_app_scenes(null, 0);
            var natives = new IntPtr[count];
            fixed (IntPtr* pointer = natives) count = (int)Math.Min((nuint)count, KoralNative.koral_app_scenes(pointer, (nuint)count));
            return natives.Take(count).Select(n => SceneBridge.Of(n)!).ToArray();
        }
    }

    public bool IsOpen(Scene scene) => scene.IsOpen;

    /// <summary>The first window's scene named <paramref name="name"/>: how a scene is found again after a reload.</summary>
    public Scene? FindScene(string name) => Scenes.FirstOrDefault(s => s.Name == name);

    /// <summary>Frame(): one frame of every scene. False once none is left.</summary>
    public bool Frame()
    {
        ThrowIfDisposed();
        MainThread.Resume();
        var running = KoralNative.koral_app_frame().AsBool();
        KoralNative.Check();
        return running;
    }

    /// <summary>Run(): frames until no scene is left.</summary>
    public int Run()
    {
        while (Frame()) { }
        return 0;
    }

    public void Quit() => KoralNative.koral_app_quit();

    public void Replace(Scene shown, string name, SceneArgs? arguments = null) =>
        KoralNative.koral_app_replace(shown.Native, name, SceneArgs.JsonOf(arguments));

    public void Push(Scene over, string name, SceneArgs? arguments = null) =>
        KoralNative.koral_app_push(over.Native, name, SceneArgs.JsonOf(arguments));

    public void Pop(Scene shown) => KoralNative.koral_app_pop(shown.Native);
    public void Close(Scene shown) => KoralNative.koral_app_close(shown.Native);

    /// <summary>Shuts every scene down, then the application.</summary>
    public void Dispose()
    {
        if (_disposed) return;
        _disposed = true;
        // What no scene owned, and nobody disposed, goes before the device does.
        IDisposable[] left;
        lock (_owned)
        {
            left = _owned.ToArray();
            _owned.Clear();
        }
        for (var i = left.Length - 1; i >= 0; --i) left[i].Dispose();
        KoralNative.koral_app_destroy();
        foreach (var handle in _factories.Values) handle.Free();
        _factories.Clear();
        Current = null;
        SynchronizationContext.SetSynchronizationContext(_previousContext);
    }

    private void ThrowIfDisposed() => ObjectDisposedException.ThrowIf(_disposed, this);

    void IResourceOwner.Own(IDisposable resource)
    {
        lock (_owned) _owned.Add(resource);
    }

    void IResourceOwner.Disown(IDisposable resource)
    {
        // By reference: Resource.Equals is false once disposed, so Remove would never find it.
        lock (_owned) _owned.RemoveAll(o => ReferenceEquals(o, resource));
    }

    internal static void Report(Scene scene, string hook, Exception exception)
    {
        Log.Error($"[{scene.GetType().Name}] {hook} threw {exception}");
        try { HookFailed?.Invoke(scene, hook, exception); }
        catch (Exception e) { Log.Error($"[koral] a HookFailed handler threw {e}"); }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static KoralSceneCallbacks MakeScene(byte* argumentsJson, IntPtr user)
    {
        var factory = (Func<SceneArgs, Scene>)GCHandle.FromIntPtr(user).Target!;
        var made = SceneBridge.Constructing = [];
        var others = SceneBridge.ConstructingOthers = [];
        try
        {
            var scene = factory(SceneArgs.FromJson(KoralNative.Text(argumentsJson)));
            foreach (var resource in made) resource.OwnedBy(scene);
            foreach (var other in others) ((IResourceOwner)scene).Own(other);
            SceneBridge.LastMade = scene;
            return SceneBridge.CallbacksFor(scene);
        }
        catch (Exception e)
        {
            for (var i = made.Count - 1; i >= 0; --i) made[i].Dispose();
            for (var i = others.Count - 1; i >= 0; --i) others[i].Dispose();
            // Every member zero: the scene is not opened, and Open says so.
            Log.Error($"[koral] a scene could not be made: {e}");
            return default;
        }
        finally
        {
            SceneBridge.Constructing = null;
            SceneBridge.ConstructingOthers = null;
        }
    }
}

/// <summary>kor::Navigator: what the current scene's window shows — after the frame, but for Open.</summary>
public static unsafe class Navigator
{
    /// <summary>Open(name, window, arguments): <paramref name="name"/> in a new window, now.</summary>
    public static Scene Open(string name, WindowSettings? window = null, SceneArgs? arguments = null)
    {
        var json = SceneArgs.JsonOf(arguments);
        return App.Opened((window ?? new WindowSettings { Title = name }).WithNative(w => KoralNative.koral_navigator_open(name, (KoralWindowSettings*)w, json)));
    }

    /// <summary>OpenOffscreen(name, target, arguments): <paramref name="name"/> offscreen, now.</summary>
    public static Scene OpenOffscreen(string name, OffscreenSettings? target = null, SceneArgs? arguments = null)
    {
        var json = SceneArgs.JsonOf(arguments);
        return App.Opened((target ?? new OffscreenSettings { Title = name }).WithNative(t => KoralNative.koral_navigator_open_offscreen(name, (KoralOffscreenSettings*)t, json)));
    }

    /// <summary>Shows <paramref name="name"/> instead; the current scene is shut down.</summary>
    public static void Replace(string name, SceneArgs? arguments = null) => KoralNative.koral_navigator_replace(name, SceneArgs.JsonOf(arguments));
    /// <summary>Shows <paramref name="name"/> over it; the current one is suspended until <see cref="Pop"/>.</summary>
    public static void Push(string name, SceneArgs? arguments = null) => KoralNative.koral_navigator_push(name, SceneArgs.JsonOf(arguments));
    /// <summary>Shuts the current scene down and shows the one under it.</summary>
    public static void Pop() => KoralNative.koral_navigator_pop();
    /// <summary>Closes the window and every scene in it.</summary>
    public static void Close() => KoralNative.koral_navigator_close();
    /// <summary>Closes every window.</summary>
    public static void Quit() => KoralNative.koral_navigator_quit();
}

/// <summary>
/// kor::ProjectConfig, read the way the runtime reads it: koral.json (from <c>--config</c>,
/// <c>KORAL_CONFIG</c>, or the nearest one at or above a directory), then the flags over it. Loading it also
/// applies what applies before the application exists — asset and shader search paths, and modules.
/// </summary>
public sealed unsafe class ProjectConfig : IDisposable
{
    private IntPtr _native;

    private ProjectConfig(IntPtr native) => _native = native;

    /// <summary>Throws for a bad file or flag; <see cref="Usage"/> lists the flags.</summary>
    public static ProjectConfig Load(IReadOnlyList<string> args, string? searchFrom = null) =>
        new(KoralNative.Check(KoralNative.koral_project_load(searchFrom ?? Directory.GetCurrentDirectory(), args.Count, args.ToArray())));

    private IntPtr Native
    {
        get
        {
            ObjectDisposedException.ThrowIf(_native == IntPtr.Zero, this);
            return _native;
        }
    }

    /// <summary>The scene to open: <c>scene</c> in koral.json or <c>--scene</c>; empty when neither says.</summary>
    public string Scene => KoralNative.Text(KoralNative.koral_project_scene(Native));
    public bool HotReload => KoralNative.koral_project_hot_reload(Native).AsBool();

    public AppSettings AppSettings
    {
        get
        {
            KoralAppSettings s;
            KoralNative.koral_project_app_settings(Native, &s);
            return new AppSettings
            {
                Api = (API)s.api,
                Platform = (WindowPlatform)s.platform,
                FramesInFlight = s.frames_in_flight,
                Gpu = s.gpu == IntPtr.Zero ? "" : KoralNative.Text((byte*)s.gpu),
                InterfaceDirectory = s.interface_directory == IntPtr.Zero ? "" : KoralNative.Text((byte*)s.interface_directory),
            };
        }
    }

    public WindowSettings WindowSettings
    {
        get
        {
            KoralWindowSettings s;
            KoralNative.koral_project_window_settings(Native, &s);
            return WindowSettings.From(s);
        }
    }

    /// <summary>The flags a project accepts, as the runtime's --help prints them.</summary>
    public static string Usage => KoralNative.Text(KoralNative.koral_project_usage());

    public void Dispose()
    {
        if (_native == IntPtr.Zero) return;
        KoralNative.koral_project_destroy(_native);
        _native = IntPtr.Zero;
    }
}
