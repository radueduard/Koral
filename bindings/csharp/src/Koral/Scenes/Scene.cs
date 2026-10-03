using System.Collections.Concurrent;
using System.Numerics;
using Koral.Native;

namespace Koral;

/// <summary>
/// kor::Scene: one screen of the program — a menu, a level, an editor. Override the hooks it needs; inside
/// them, <see cref="Window"/>, <see cref="Input"/>, <see cref="Time"/> and <see cref="Debug"/> are this
/// scene's own, as <c>Window::</c> and the rest are in a C++ scene.
/// </summary>
/// <example>
/// <code>
/// public sealed class Level : Scene
/// {
///     [Keep] private Vector3 _player;   // kept across a hot reload
///
///     protected override void Initialize()
///     {
///         Input.BindAxis("MoveX", Key.eD, -(InputSource)Key.eA, GamepadAxis.eLeftX);
///         Graph.Add(new DebugDrawPass(SceneDebug, () => _camera));
///     }
///
///     protected override void Update()
///     {
///         _player.X += Input.Axis("MoveX") * Time.FrameTime;
///         Debug.Sphere(_player, 0.5f);
///         if (Input.IsKeyPressed(Key.eEsc)) Navigator.Quit();
///     }
/// }
/// </code>
/// </example>
public abstract unsafe partial class Scene : IResourceOwner
{
    private readonly List<IDisposable> _resources = [];

    internal IntPtr Native;
    /// <summary>A weak reference to the native scene (Scene::Life): what tells a scene gone from one made where it was.</summary>
    internal IntPtr Life;
    internal readonly SceneSynchronizationContext Context;

    protected Scene() => Context = new SceneSynchronizationContext(this);

    /// <summary>Whether this is a scene written in C#, or one of Koral's own (from a C++ library).</summary>
    internal virtual bool IsManaged => true;

    // ---- the hooks, in the order a frame calls them --------------------------------------------------

    /// <summary>Once, with the window there. Where resources are made.</summary>
    protected internal virtual void Initialize() { }
    /// <summary>Once per <see cref="Time.FixedDeltaTime"/> of the scene's time (0 to 8 times a frame).</summary>
    protected internal virtual void FixedUpdate() { }
    /// <summary>Once a frame.</summary>
    protected internal virtual void Update() { }
    /// <summary>After every module's late update.</summary>
    protected internal virtual void LateUpdate() { }
    /// <summary>Records the frame's own work; the scene's graph runs ahead of it.</summary>
    protected internal virtual void Render(CommandBuffer commandBuffer) { }
    /// <summary>The frame after the window changed size.</summary>
    protected internal virtual void OnResize(UVec2 extent) { }
    /// <summary>Another scene was pushed over this one.</summary>
    protected internal virtual void OnSuspend() { }
    /// <summary>The scene over this one was popped.</summary>
    protected internal virtual void OnResume() { }
    /// <summary>The window was asked to close; return false to keep it open.</summary>
    protected internal virtual bool OnCloseRequested() => true;
    /// <summary>Once, before the scene is destroyed, with everything still alive.</summary>
    protected internal virtual void Shutdown() { }

    /// <summary>
    /// kor::Scene::State: an object whose fields and properties are the state kept across a reload, by name.
    /// Null (the default) keeps the scene's <see cref="KeepAttribute"/> members instead. Return a class
    /// instance the scene holds: a struct would be a copy, and loading into it would change nothing.
    /// </summary>
    protected internal virtual object? State() => null;

    /// <summary>SaveState: <see cref="State"/> (or the <see cref="KeepAttribute"/> members), as JSON.</summary>
    public virtual string SaveState() => KeptState.Save(this);

    /// <summary>LoadState: what SaveState saved, loaded back — after the constructor, before Initialize, on a reload.</summary>
    public virtual void LoadState(string json) => KeptState.Load(this, json);

    // ---- what a scene has ------------------------------------------------------------------------------

    private IntPtr Open
    {
        get
        {
            if (!IsOpen) throw new KoralException($"the scene{(Native == IntPtr.Zero ? " is not open yet" : " is no longer open")}");
            return Native;
        }
    }

    /// <summary>Still open: shown, or suspended under another scene. One a reload replaced is not.</summary>
    public bool IsOpen => Native != IntPtr.Zero && Life != IntPtr.Zero
                          && KoralNative.koral_scene_life_scene(Life) == Native && KoralNative.koral_app_is_open(Native).AsBool();

    ~Scene() => ForgetNative();

    /// <summary>Lets go of the native scene: it is gone, or this object no longer means it.</summary>
    internal void ForgetNative()
    {
        if (Life != IntPtr.Zero) KoralNative.koral_scene_life_release(Life);
        Life = IntPtr.Zero;
        Native = IntPtr.Zero;
    }

    public string Name => IsOpen ? KoralNative.Text(KoralNative.koral_scene_name(Native)) : "";

    /// <summary>Graph(): the scene's frame graph.</summary>
    public FrameGraph Graph => new(KoralNative.koral_scene_graph(Open));

    public Koral.Window SceneWindow => new(KoralNative.koral_scene_scene_window(Open));
    public Koral.Input SceneInput => new(KoralNative.koral_scene_scene_input(Open));
    public Koral.Time SceneTime => new(KoralNative.koral_scene_scene_time(Open));
    public DebugDraw SceneDebug => new(KoralNative.koral_scene_scene_debug(Open));
    public bool HasInterface => KoralNative.koral_scene_has_interface(Open).AsBool();

    /// <summary>AddView(name, target): the scene drawn again, into an image of its own, by a graph of its own.</summary>
    public View AddView(string name, OffscreenSettings? target = null)
    {
        var scene = Open;
        return new View(KoralNative.Check((target ?? new OffscreenSettings { Title = name })
            .WithNative(t => KoralNative.koral_scene_add_view(scene, name, (KoralOffscreenSettings*)t))));
    }

    public void RemoveView(string name) => KoralNative.koral_scene_remove_view(Open, name);

    public View? FindView(string name)
    {
        var view = KoralNative.koral_scene_find_view(Open, name);
        return view == IntPtr.Zero ? null : new View(view);
    }

    public IReadOnlyList<View> Views
    {
        get
        {
            var count = KoralNative.koral_scene_view_count(Open);
            var views = new View[count];
            for (uint i = 0; i < count; ++i) views[i] = new View(KoralNative.koral_scene_view(Native, i));
            return views;
        }
    }

    /// <summary>Scene::Current(): the scene whose code is running on this thread, or null.</summary>
    public static Scene? Current => SceneBridge.Of(KoralNative.koral_scene_current());

    public override string ToString() => $"{GetType().Name} '{Name}'";

    // ---- resources it made -----------------------------------------------------------------------------------

    void IResourceOwner.Own(IDisposable resource)
    {
        lock (_resources) _resources.Add(resource);
    }

    void IResourceOwner.Disown(IDisposable resource)
    {
        // By reference: Resource.Equals is false once disposed, so Remove would never find it.
        lock (_resources) _resources.RemoveAll(o => ReferenceEquals(o, resource));
    }

    internal void ReleaseResources()
    {
        IDisposable[] left;
        lock (_resources)
        {
            left = _resources.ToArray();
            _resources.Clear();
        }
        for (var i = left.Length - 1; i >= 0; --i) left[i].Dispose();
    }

    // ---- the current scene's own, as Window::, Input::, Time:: and Debug:: ------------------------------------

    [ThreadStatic] private static (IntPtr Native, Koral.Window Object) t_window;
    [ThreadStatic] private static (IntPtr Native, Koral.Input Object) t_input;
    [ThreadStatic] private static (IntPtr Native, Koral.Time Object) t_time;
    [ThreadStatic] private static (IntPtr Native, DebugDraw Object) t_debug;

    private static IntPtr CurrentOf(IntPtr native, string what) =>
        native != IntPtr.Zero ? native : throw new KoralException($"{what} is the current scene's, and no scene is current here");

    /// <summary>kor::Scene::Window: the current scene's window.</summary>
    public static class Window
    {
        public static Koral.Window Get()
        {
            var native = CurrentOf(KoralNative.koral_current_window(), "Window");
            if (t_window.Native != native) t_window = (native, new Koral.Window(native));
            return t_window.Object;
        }

        public static UVec2 Extent => Get().Extent;
        public static bool HasResized => Get().HasResized;
        public static bool IsPaused => Get().IsPaused;
        public static bool IsFocused => Get().IsFocused;
        public static string Title => Get().Title;
        public static void SetTitle(string title) => Get().SetTitle(title);
        public static void SetIcon(string icon) => Get().SetIcon(icon);
        public static void Close() => Get().Close();
        public static Framebuffer? DefaultFramebuffer => Get().DefaultFramebuffer;
    }

    /// <summary>kor::Scene::Input: the current scene's input — only the focused scene's is live.</summary>
    public static class Input
    {
        public static Koral.Input Get()
        {
            var native = CurrentOf(KoralNative.koral_current_input(), "Input");
            if (t_input.Native != native) t_input = (native, new Koral.Input(native));
            return t_input.Object;
        }

        public static KeyState StateOf(Key key) => Get().StateOf(key);
        public static KeyState MouseButtonState(MouseButton button) => Get().MouseButtonState(button);
        public static bool IsKeyPressed(Key key) => Get().IsKeyPressed(key);
        public static bool IsKeyHeld(Key key) => Get().IsKeyHeld(key);
        public static bool IsKeyReleased(Key key) => Get().IsKeyReleased(key);
        public static bool IsMouseButtonPressed(MouseButton button) => Get().IsMouseButtonPressed(button);
        public static bool IsMouseButtonHeld(MouseButton button) => Get().IsMouseButtonHeld(button);
        public static bool IsMouseButtonReleased(MouseButton button) => Get().IsMouseButtonReleased(button);
        public static Key? FirstKeyPressed() => Get().FirstKeyPressed();
        public static MouseButton? FirstMouseButtonPressed() => Get().FirstMouseButtonPressed();
        public static bool InterfaceWantsMouse => Get().InterfaceWantsMouse;
        public static bool InterfaceWantsKeyboard => Get().InterfaceWantsKeyboard;
        public static string TypedText => Get().TypedText;
        public static bool IsKeyRepeated(Key key) => Get().IsKeyRepeated(key);
        public static Vector2 MousePosition => Get().MousePosition;
        public static Vector2 MousePositionDelta => Get().MousePositionDelta;
        public static Vector2 MouseScrollDelta => Get().MouseScrollDelta;
        public static Vector2 LastMousePosition => Get().LastMousePosition;
        public static void SetCursorMode(Koral.Input.CursorMode mode) => Get().SetCursorMode(mode);
        public static Koral.Input.CursorMode CurrentCursorMode => Get().CurrentCursorMode;
        public static string Describe(Key key) => Koral.Input.Describe(key);
        public static string Describe(MouseButton button) => Koral.Input.Describe(button);
        public static bool IsGamepadConnected(int pad = 0) => Get().IsGamepadConnected(pad);
        public static KeyState GamepadButtonState(GamepadButton button, int pad = 0) => Get().GamepadButtonState(button, pad);
        public static bool IsGamepadButtonPressed(GamepadButton button, int pad = 0) => Get().IsGamepadButtonPressed(button, pad);
        public static bool IsGamepadButtonHeld(GamepadButton button, int pad = 0) => Get().IsGamepadButtonHeld(button, pad);
        public static float GamepadAxisValue(GamepadAxis axis, int pad = 0) => Get().GamepadAxisValue(axis, pad);
        public static void BindAction(string action, params ReadOnlySpan<InputSource> sources) => Get().BindAction(action, sources);
        public static void BindAxis(string axis, params ReadOnlySpan<InputSource> sources) => Get().BindAxis(axis, sources);
        public static KeyState ActionState(string action) => Get().ActionState(action);
        public static bool IsActionPressed(string action) => Get().IsActionPressed(action);
        public static bool IsActionHeld(string action) => Get().IsActionHeld(action);
        public static bool IsActionReleased(string action) => Get().IsActionReleased(action);
        public static float Axis(string axis) => Get().Axis(axis);
        public static Vector2 Axis2D(string x, string y) => Get().Axis2D(x, y);
    }

    /// <summary>kor::Scene::Debug: the current scene's debug lines.</summary>
    public static class Debug
    {
        public static DebugDraw Get()
        {
            var native = CurrentOf(KoralNative.koral_current_debug(), "Debug");
            if (t_debug.Native != native) t_debug = (native, new DebugDraw(native));
            return t_debug.Object;
        }

        public static void Line(Vector3 from, Vector3 to, DebugStyle? style = null) => Get().Line(from, to, style);
        public static void Box(Vector3 min, Vector3 max, DebugStyle? style = null) => Get().Box(min, max, style);
        public static void Box(Matrix4x4 transform, DebugStyle? style = null) => Get().Box(transform, style);
        public static void Circle(Vector3 center, Vector3 normal, float radius, DebugStyle? style = null) => Get().Circle(center, normal, radius, style);
        public static void Sphere(Vector3 center, float radius, DebugStyle? style = null) => Get().Sphere(center, radius, style);
        public static void Arrow(Vector3 from, Vector3 to, DebugStyle? style = null) => Get().Arrow(from, to, style);
        public static void Point(Vector3 position, float size = 0.1f, DebugStyle? style = null) => Get().Point(position, size, style);
        public static void Axes(Matrix4x4 transform, float size = 1f, float duration = 0f) => Get().Axes(transform, size, duration);
        public static void Grid(Vector3 center, float size, int cells, DebugStyle? style = null) => Get().Grid(center, size, cells, style);
        public static void Frustum(Matrix4x4 viewProjection, DebugStyle? style = null) => Get().Frustum(viewProjection, style);
    }

    /// <summary>kor::Scene::Time: the current scene's clock.</summary>
    public static class Time
    {
        public static Koral.Time Get()
        {
            var native = CurrentOf(KoralNative.koral_current_time(), "Time");
            if (t_time.Native != native) t_time = (native, new Koral.Time(native));
            return t_time.Object;
        }

        /// <summary>Seconds since the last frame, scaled; inside FixedUpdate, the fixed step.</summary>
        public static float FrameTime => Get().FrameTime;
        public static float UnscaledFrameTime => Get().UnscaledFrameTime;
        public static float FixedDeltaTime => Get().FixedDeltaTime;
        public static void SetFixedDeltaTime(float seconds) => Get().SetFixedDeltaTime(seconds);
        public static float FixedStepFraction => Get().FixedStepFraction;
        public static float Elapsed => Get().Elapsed;
        public static ulong FrameCount => Get().FrameCount;
        public static float TimeScale => Get().TimeScale;
        public static void SetTimeScale(float scale) => Get().SetTimeScale(scale);
    }
}

/// <summary>A scene of Koral's own — from a C++ scene library — as C# sees it: its members, but not its hooks.</summary>
internal sealed class ForeignScene : Scene
{
    internal override bool IsManaged => false;
    public override unsafe string SaveState() => KoralNative.Text(KoralNative.koral_scene_save_state(Native));
    public override void LoadState(string json) => KoralNative.Check(KoralNative.koral_scene_load_state(Native, json));
}

/// <summary>
/// Kept across a reload: a field or property of a scene, saved by <see cref="Scene.SaveState"/> by name.
/// Anything System.Text.Json can write — numbers, strings, vectors, lists, plain classes.
/// </summary>
[AttributeUsage(AttributeTargets.Field | AttributeTargets.Property)]
public sealed class KeepAttribute : Attribute;

/// <summary>Registers a scene class under a name other than its own.</summary>
[AttributeUsage(AttributeTargets.Class, Inherited = false)]
public sealed class SceneAttribute(string name) : Attribute
{
    public string Name { get; } = name;
}
