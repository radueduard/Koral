using System.Text.Json;
using System.Text.Json.Serialization;
using Koral.Native;

namespace Koral;

// The objects a scene has — its window, input, clock and debug lines, and its views — each a C# face on
// the C++ object, valid for as long as that is (the scene's life, for all but a view).

/// <summary>kor::Window: an OS window, or an offscreen one (an image).</summary>
public sealed unsafe partial class Window
{
    internal readonly IntPtr Native;
    internal Window(IntPtr native) => Native = native;

    public bool ShouldClose => KoralNative.koral_window_should_close(Native).AsBool();
    public void Close() => KoralNative.koral_window_close(Native);
    public bool IsOffscreen => KoralNative.koral_window_is_offscreen(Native).AsBool();
    /// <summary>What it shows: its swap chain's current image, or its offscreen one.</summary>
    public Image? Image => Resource.Wrap<Image>(KoralNative.koral_window_image(Native));
    public void Resize(UVec2 extent) => KoralNative.koral_window_resize(Native, extent.X, extent.Y);

    public UVec2 Extent
    {
        get
        {
            uint x, y;
            KoralNative.koral_window_extent(Native, &x, &y);
            return new UVec2(x, y);
        }
    }

    public bool IsPaused => KoralNative.koral_window_is_paused(Native).AsBool();
    public bool IsResizable => KoralNative.koral_window_is_resizable(Native).AsBool();
    public bool IsFullscreen => KoralNative.koral_window_is_fullscreen(Native).AsBool();
    public bool IsDecorated => KoralNative.koral_window_is_decorated(Native).AsBool();
    public bool IsVSync => KoralNative.koral_window_is_vsync(Native).AsBool();
    public bool IsFramebufferTransparent => KoralNative.koral_window_is_framebuffer_transparent(Native).AsBool();
    public Format PixelFormat => (Format)KoralNative.koral_window_pixel_format(Native);
    public void Pause() => KoralNative.koral_window_pause(Native);
    public void Unpause() => KoralNative.koral_window_unpause(Native);
    public void SetTitle(string title) => KoralNative.koral_window_set_title(Native, title);
    public string Title => KoralNative.Text(KoralNative.koral_window_title(Native));
    public Framebuffer? DefaultFramebuffer => Resource.Wrap<Framebuffer>(KoralNative.koral_window_default_framebuffer(Native));
    public bool HasResized => KoralNative.koral_window_has_resized(Native).AsBool();
    public void SetIcon(string iconPath) => KoralNative.koral_window_set_icon(Native, iconPath);
    public bool IsFocused => KoralNative.koral_window_is_focused(Native).AsBool();
    public bool IsShownThisFrame => KoralNative.koral_window_is_shown_this_frame(Native).AsBool();
}

/// <summary>
/// kor::InputSource: a key, mouse button, gamepad button or gamepad axis, times a scale — made from any of
/// them, as the C++ constructors are implicit: <c>BindAction("Jump", Key.eSpace, GamepadButton.eA)</c>.
/// </summary>
public readonly partial struct InputSource(InputSource.Kind kind, ushort code, float scale = 1f)
{
    public Kind SourceKind { get; } = kind;
    public ushort Code { get; } = code;
    /// <summary>For an axis: what it contributes, times the source's value. -1 turns it round.</summary>
    public float Scale { get; } = scale;

    public InputSource(Key key, float scale = 1f) : this(Kind.eKey, (ushort)key, scale) { }
    public InputSource(MouseButton button, float scale = 1f) : this(Kind.eMouseButton, (ushort)button, scale) { }
    public InputSource(GamepadButton button, float scale = 1f) : this(Kind.eGamepadButton, (ushort)button, scale) { }
    public InputSource(GamepadAxis axis, float scale = 1f) : this(Kind.eGamepadAxis, (ushort)axis, scale) { }

    public static implicit operator InputSource(Key key) => new(key);
    public static implicit operator InputSource(MouseButton button) => new(button);
    public static implicit operator InputSource(GamepadButton button) => new(button);
    public static implicit operator InputSource(GamepadAxis axis) => new(axis);

    /// <summary>The same source, turned round: <c>-(InputSource)Key.eA</c> is "-Key.A".</summary>
    public static InputSource operator -(InputSource source) => new(source.SourceKind, source.Code, -source.Scale);

    internal KoralInputSource Native => new() { kind = (uint)SourceKind, code = Code, scale = Scale };

    /// <summary>Its name, as bindings are saved: "Key.Space", "-GamepadAxis.LeftY", "Key.W*0.5".</summary>
    public unsafe string Name
    {
        get
        {
            var native = Native;
            return KoralNative.Text(KoralNative.koral_input_source_name(&native));
        }
    }

    /// <summary>Parse(name): the source a name means, or null for one that means none.</summary>
    public static unsafe InputSource? Parse(string name)
    {
        KoralInputSource s;
        return KoralNative.koral_input_source_parse(name, &s).AsBool() ? new InputSource((Kind)s.kind, (ushort)s.code, s.scale) : null;
    }

    public override string ToString() => Name;
}

/// <summary>kor::InputBindings: every action and axis, by the names of their sources — what a settings file keeps.</summary>
public sealed record InputBindings
{
    /// <summary>kor::InputBindings::Entry.</summary>
    public sealed record Entry
    {
        [JsonPropertyName("name")] public string Name { get; init; } = "";
        [JsonPropertyName("sources")] public List<string> Sources { get; init; } = [];
    }

    [JsonPropertyName("actions")] public List<Entry> Actions { get; init; } = [];
    [JsonPropertyName("axes")] public List<Entry> Axes { get; init; } = [];
}

/// <summary>kor::Input: keys, mouse and gamepads, and the actions and axes bound to them.</summary>
public sealed unsafe partial class Input
{
    internal readonly IntPtr Native;
    internal Input(IntPtr native) => Native = native;

    public const int MaxGamepads = 4;

    public KeyState StateOf(Key key) => (KeyState)KoralNative.koral_input_state_of(Native, (uint)key);
    public KeyState MouseButtonState(MouseButton button) => (KeyState)KoralNative.koral_input_mouse_button_state(Native, (uint)button);
    public bool IsKeyPressed(Key key) => StateOf(key) == KeyState.ePressed;
    public bool IsKeyHeld(Key key) => StateOf(key) == KeyState.eHeld;
    public bool IsKeyReleased(Key key) => StateOf(key) == KeyState.eReleased;
    public bool IsMouseButtonPressed(MouseButton button) => MouseButtonState(button) == KeyState.ePressed;
    public bool IsMouseButtonHeld(MouseButton button) => MouseButtonState(button) == KeyState.eHeld;
    public bool IsMouseButtonReleased(MouseButton button) => MouseButtonState(button) == KeyState.eReleased;
    public static string Describe(Key key) => KoralNative.Text(KoralNative.koral_input_describe_key((uint)key));
    public static string Describe(MouseButton button) => KoralNative.Text(KoralNative.koral_input_describe_mouse_button((uint)button));

    public Key? FirstKeyPressed()
    {
        uint key;
        return KoralNative.koral_input_first_key_pressed(Native, &key).AsBool() ? (Key)key : null;
    }

    public MouseButton? FirstMouseButtonPressed()
    {
        uint button;
        return KoralNative.koral_input_first_mouse_button_pressed(Native, &button).AsBool() ? (MouseButton)button : null;
    }

    public bool InterfaceWantsMouse => KoralNative.koral_input_interface_wants_mouse(Native).AsBool();
    public bool InterfaceWantsKeyboard => KoralNative.koral_input_interface_wants_keyboard(Native).AsBool();
    public Vec2 MousePosition { get { Vec2 v; KoralNative.koral_input_mouse_position(Native, &v.X, &v.Y); return v; } }
    public Vec2 MousePositionDelta { get { Vec2 v; KoralNative.koral_input_mouse_position_delta(Native, &v.X, &v.Y); return v; } }
    public Vec2 MouseScrollDelta { get { Vec2 v; KoralNative.koral_input_mouse_scroll_delta(Native, &v.X, &v.Y); return v; } }
    public Vec2 LastMousePosition { get { Vec2 v; KoralNative.koral_input_last_mouse_position(Native, &v.X, &v.Y); return v; } }
    public void SetCursorMode(CursorMode mode) => KoralNative.koral_input_set_cursor_mode(Native, (uint)mode);
    public CursorMode CurrentCursorMode => (CursorMode)KoralNative.koral_input_current_cursor_mode(Native);

    public bool IsGamepadConnected(int pad = 0) => KoralNative.koral_input_is_gamepad_connected(Native, pad).AsBool();
    public string GamepadName(int pad = 0) => KoralNative.Text(KoralNative.koral_input_gamepad_name(Native, pad));
    public KeyState GamepadButtonState(GamepadButton button, int pad = 0) => (KeyState)KoralNative.koral_input_gamepad_button_state(Native, (uint)button, pad);
    public bool IsGamepadButtonPressed(GamepadButton button, int pad = 0) => GamepadButtonState(button, pad) == KeyState.ePressed;
    public bool IsGamepadButtonHeld(GamepadButton button, int pad = 0) => GamepadButtonState(button, pad) == KeyState.eHeld;
    public bool IsGamepadButtonReleased(GamepadButton button, int pad = 0) => GamepadButtonState(button, pad) == KeyState.eReleased;
    public float GamepadAxisValue(GamepadAxis axis, int pad = 0) => KoralNative.koral_input_gamepad_axis_value(Native, (uint)axis, pad);
    public void SetGamepadDeadZone(float deadZone) => KoralNative.koral_input_set_gamepad_dead_zone(Native, deadZone);

    public void BindAction(string action, params ReadOnlySpan<InputSource> sources)
    {
        var native = new KoralInputSource[sources.Length];
        for (var i = 0; i < native.Length; ++i) native[i] = sources[i].Native;
        fixed (KoralInputSource* pointer = native) KoralNative.koral_input_bind_action(Native, action, pointer, (nuint)native.Length);
    }

    public void BindAxis(string axis, params ReadOnlySpan<InputSource> sources)
    {
        var native = new KoralInputSource[sources.Length];
        for (var i = 0; i < native.Length; ++i) native[i] = sources[i].Native;
        fixed (KoralInputSource* pointer = native) KoralNative.koral_input_bind_axis(Native, axis, pointer, (nuint)native.Length);
    }

    public KeyState ActionState(string action) => (KeyState)KoralNative.koral_input_action_state(Native, action);
    public bool IsActionPressed(string action) => ActionState(action) == KeyState.ePressed;
    public bool IsActionHeld(string action) => ActionState(action) == KeyState.eHeld;
    public bool IsActionReleased(string action) => ActionState(action) == KeyState.eReleased;
    public float Axis(string axis) => KoralNative.koral_input_axis(Native, axis);

    public Vec2 Axis2D(string x, string y)
    {
        Vec2 v;
        KoralNative.koral_input_axis_2d(Native, x, y, &v.X, &v.Y);
        return v;
    }

    public InputBindings Bindings =>
        JsonSerializer.Deserialize<InputBindings>(KoralNative.Text(KoralNative.koral_input_bindings(Native))) ?? new InputBindings();

    public void SetBindings(InputBindings bindings) =>
        KoralNative.Check(KoralNative.koral_input_set_bindings(Native, JsonSerializer.Serialize(bindings)));

    public void FeedKey(Key key, bool down) => KoralNative.koral_input_feed_key(Native, (uint)key, KoralNative.Bool(down));
    public void FeedMouseButton(MouseButton button, bool down) => KoralNative.koral_input_feed_mouse_button(Native, (uint)button, KoralNative.Bool(down));
    public void FeedMousePosition(Vec2 position) => KoralNative.koral_input_feed_mouse_position(Native, position.X, position.Y);
    /// <summary>Input::FeedText: text typed, as <see cref="TypedText"/> will hold it.</summary>
    public void FeedText(string text) => KoralNative.koral_input_feed_text(Native, text);
    public void FeedKeyRepeat(Key key) => KoralNative.koral_input_feed_key_repeat(Native, (uint)key);
    /// <summary>Input::TypedText: the text typed this frame — what a text field inserts.</summary>
    public string TypedText => KoralNative.Text(KoralNative.koral_input_typed_text(Native));
    /// <summary>Input::IsKeyRepeated: whether a held key repeated this frame.</summary>
    public bool IsKeyRepeated(Key key) => KoralNative.koral_input_is_key_repeated(Native, (uint)key).AsBool();
    public void FeedMouseDelta(Vec2 delta) => KoralNative.koral_input_feed_mouse_delta(Native, delta.X, delta.Y);
    public void FeedScroll(Vec2 delta) => KoralNative.koral_input_feed_scroll(Native, delta.X, delta.Y);
    public void FeedGamepadButton(GamepadButton button, bool down, int pad = 0) => KoralNative.koral_input_feed_gamepad_button(Native, (uint)button, KoralNative.Bool(down), pad);
    public void FeedGamepadAxis(GamepadAxis axis, float value, int pad = 0) => KoralNative.koral_input_feed_gamepad_axis(Native, (uint)axis, value, pad);
    public void ReleaseAll() => KoralNative.koral_input_release_all(Native);
}

/// <summary>kor::Time: a scene's clock — each scene has its own.</summary>
public sealed class Time
{
    internal readonly IntPtr Native;
    internal Time(IntPtr native) => Native = native;

    public const uint MaxFixedSteps = 8;

    /// <summary>Seconds since the last frame, scaled; inside FixedUpdate, the fixed step.</summary>
    public float FrameTime => KoralNative.koral_time_frame_time(Native);
    public float UnscaledFrameTime => KoralNative.koral_time_unscaled_frame_time(Native);
    public float FixedDeltaTime => KoralNative.koral_time_fixed_delta_time(Native);
    public void SetFixedDeltaTime(float seconds) => KoralNative.koral_time_set_fixed_delta_time(Native, seconds);
    public float FixedStepFraction => KoralNative.koral_time_fixed_step_fraction(Native);
    public bool InFixedStep => KoralNative.koral_time_in_fixed_step(Native).AsBool();
    public float Elapsed => KoralNative.koral_time_elapsed(Native);
    public ulong FrameCount => KoralNative.koral_time_frame_count(Native);
    public float TimeScale => KoralNative.koral_time_time_scale(Native);
    public void SetTimeScale(float scale) => KoralNative.koral_time_set_time_scale(Native, scale);
}

/// <summary>kor::DebugStyle (DebugDraw::Style): how a debug shape is drawn.</summary>
public record struct DebugStyle()
{
    public Vec4 Color { get; init; } = Vec4.One;
    /// <summary>Seconds of the scene's time to keep drawing it; 0 is this frame only.</summary>
    public float Duration { get; init; }
    /// <summary>Drawn over everything, rather than hidden by what is in front of it.</summary>
    public bool OnTop { get; init; }
    /// <summary>The colour to fill the shape with; alpha below 1 is see-through, and 0 (the default) no fill.</summary>
    public Vec4 Fill { get; init; }
    /// <summary>Draws the outline; false for a shape that is only its fill.</summary>
    public bool Outline { get; init; } = true;
    /// <summary>How wide its lines are, in pixels, however near or far.</summary>
    public float LineWidth { get; init; } = 1f;

    internal unsafe KoralDebugStyle Native
    {
        get
        {
            var s = new KoralDebugStyle { duration = Duration, on_top = KoralNative.Bool(OnTop), fill_only = KoralNative.Bool(!Outline), line_width = LineWidth };
            s.color[0] = Color.X; s.color[1] = Color.Y; s.color[2] = Color.Z; s.color[3] = Color.W;
            s.fill[0] = Fill.X; s.fill[1] = Fill.Y; s.fill[2] = Fill.Z; s.fill[3] = Fill.W;
            return s;
        }
    }
}

/// <summary>kor::GizmoPointer: the pointer a gizmo is used with, in the pixels of the image its camera draws.</summary>
/// <param name="Position">Where it is, from the image's top-left; null when it is not over the image (or is something else's).</param>
/// <param name="Viewport">The image's size, in pixels.</param>
/// <param name="Down">The button that drags a handle is down.</param>
/// <param name="Pressed">...and went down this frame: what grabs a handle.</param>
public record struct GizmoPointer(Vec2? Position, Vec2 Viewport, bool Down, bool Pressed)
{
    internal unsafe KoralGizmoPointer Native
    {
        get
        {
            var p = new KoralGizmoPointer { has_position = KoralNative.Bool(Position.HasValue), down = KoralNative.Bool(Down), pressed = KoralNative.Bool(Pressed) };
            if (Position is { } at) { p.position[0] = at.X; p.position[1] = at.Y; }
            p.viewport[0] = Viewport.X; p.viewport[1] = Viewport.Y;
            return p;
        }
    }
}

/// <summary>kor::GizmoOptions: how a gizmo looks and snaps.</summary>
public record struct GizmoOptions()
{
    public GizmoSpace Space { get; init; } = GizmoSpace.eWorld;
    /// <summary>Its length on screen, in pixels.</summary>
    public float Size { get; init; } = 100f;
    /// <summary>Steps it moves in: world units translating, degrees rotating, a factor's step scaling. 0 is none.</summary>
    public float Snap { get; init; }

    internal KoralGizmoOptions Native => new() { space = (uint)Space, size = Size, snap = Snap };
}

/// <summary>kor::DebugDraw: lines a scene draws to see what it is doing.</summary>
public sealed unsafe class DebugDraw
{
    internal readonly IntPtr Native;
    internal DebugDraw(IntPtr native) => Native = native;

    private static KoralDebugStyle S(DebugStyle? style) => (style ?? new DebugStyle()).Native;

    public void Line(Vec3 from, Vec3 to, DebugStyle? style = null) { var s = S(style); KoralNative.koral_debug_line(Native, (float*)&from, (float*)&to, &s); }
    /// <summary>An axis-aligned box.</summary>
    public void Box(Vec3 min, Vec3 max, DebugStyle? style = null) { var s = S(style); KoralNative.koral_debug_box(Native, (float*)&min, (float*)&max, &s); }
    /// <summary>The unit cube (-0.5 to 0.5) moved by <paramref name="transform"/>: a box in any orientation.</summary>
    public void Box(Mat4 transform, DebugStyle? style = null) { var s = S(style); KoralNative.koral_debug_box_transform(Native, (float*)&transform, &s); }
    public void Circle(Vec3 center, Vec3 normal, float radius, DebugStyle? style = null, int segments = 32)
    {
        var s = S(style);
        KoralNative.koral_debug_circle(Native, (float*)&center, (float*)&normal, radius, &s, segments);
    }
    /// <summary>Three circles, one per axis.</summary>
    public void Sphere(Vec3 center, float radius, DebugStyle? style = null, int segments = 32) { var s = S(style); KoralNative.koral_debug_sphere(Native, (float*)&center, radius, &s, segments); }
    public void Arrow(Vec3 from, Vec3 to, DebugStyle? style = null) { var s = S(style); KoralNative.koral_debug_arrow(Native, (float*)&from, (float*)&to, &s); }
    /// <summary>A small cross: a point you can see.</summary>
    public void Point(Vec3 position, float size = 0.1f, DebugStyle? style = null) { var s = S(style); KoralNative.koral_debug_point(Native, (float*)&position, size, &s); }
    /// <summary><paramref name="transform"/>'s X, Y and Z axes, in red, green and blue.</summary>
    public void Axes(Mat4 transform, float size = 1f, float duration = 0f) => KoralNative.koral_debug_axes(Native, (float*)&transform, size, duration);
    /// <summary>A grid of <paramref name="cells"/> by <paramref name="cells"/> on the XZ plane.</summary>
    public void Grid(Vec3 center, float size, int cells, DebugStyle? style = null) { var s = S(style); KoralNative.koral_debug_grid(Native, (float*)&center, size, cells, &s); }
    /// <summary>What a camera with <paramref name="viewProjection"/> sees: its frustum's twelve edges.</summary>
    public void Frustum(Mat4 viewProjection, DebugStyle? style = null) { var s = S(style); KoralNative.koral_debug_frustum(Native, (float*)&viewProjection, &s); }
    public void Triangle(Vec3 a, Vec3 b, Vec3 c, DebugStyle? style = null) { var s = S(style); KoralNative.koral_debug_triangle(Native, (float*)&a, (float*)&b, (float*)&c, &s); }
    /// <summary>Four corners, in order around the edge.</summary>
    public void Quad(Vec3 a, Vec3 b, Vec3 c, Vec3 d, DebugStyle? style = null)
    {
        var s = S(style);
        KoralNative.koral_debug_quad(Native, (float*)&a, (float*)&b, (float*)&c, (float*)&d, &s);
    }
    /// <summary>A rectangle of <paramref name="size"/> facing along <paramref name="normal"/>.</summary>
    public void Plane(Vec3 center, Vec3 normal, Vec2 size, DebugStyle? style = null)
    {
        var s = S(style);
        KoralNative.koral_debug_plane(Native, (float*)&center, (float*)&normal, (float*)&size, &s);
    }
    public void Cylinder(Vec3 from, Vec3 to, float radius, DebugStyle? style = null, int segments = 24)
    {
        var s = S(style);
        KoralNative.koral_debug_cylinder(Native, (float*)&from, (float*)&to, radius, &s, segments);
    }
    /// <summary>A cone with its base's centre at <paramref name="baseCenter"/> and its point at <paramref name="tip"/>.</summary>
    public void Cone(Vec3 baseCenter, Vec3 tip, float radius, DebugStyle? style = null, int segments = 24)
    {
        var s = S(style);
        KoralNative.koral_debug_cone(Native, (float*)&baseCenter, (float*)&tip, radius, &s, segments);
    }
    public void Capsule(Vec3 from, Vec3 to, float radius, DebugStyle? style = null, int segments = 24)
    {
        var s = S(style);
        KoralNative.koral_debug_capsule(Native, (float*)&from, (float*)&to, radius, &s, segments);
    }
    /// <summary>A camera: the pyramid it sees through, <paramref name="size"/> deep, with a triangle on top for up.</summary>
    public void Camera(Mat4 view, Mat4 projection, float size = 1f, DebugStyle? style = null)
    {
        var s = S(style);
        KoralNative.koral_debug_camera(Native, (float*)&view, (float*)&projection, size, &s);
    }
    /// <summary>A star where it is, and the sphere it reaches to (none when <paramref name="range"/> is 0: no limit).</summary>
    public void PointLight(Vec3 position, float range, DebugStyle? style = null) { var s = S(style); KoralNative.koral_debug_point_light(Native, (float*)&position, range, &s); }
    /// <summary>The cone it lights; angles from the centre to the edge, in radians.</summary>
    public void SpotLight(Vec3 position, Vec3 direction, float range, float outerAngle, float innerAngle = 0f, DebugStyle? style = null)
    {
        var s = S(style);
        KoralNative.koral_debug_spot_light(Native, (float*)&position, (float*)&direction, range, outerAngle, innerAngle, &s);
    }
    /// <summary>The sun: a disc at <paramref name="position"/> with its rays.</summary>
    public void DirectionalLight(Vec3 position, Vec3 direction, float size = 1f, DebugStyle? style = null)
    {
        var s = S(style);
        KoralNative.koral_debug_directional_light(Native, (float*)&position, (float*)&direction, size, &s);
    }

    /// <summary>
    /// kor::DebugDraw::Gizmo: handles on <paramref name="transform"/>, the one under <paramref name="pointer"/> dragged while
    /// its button is down. Call it every frame the thing is selected. Returns whether <paramref name="transform"/> changed.
    /// </summary>
    public bool Gizmo(GizmoMode mode, ref Mat4 transform, Mat4 viewProjection, GizmoPointer pointer,
                      GizmoOptions? options = null, ulong id = 0)
    {
        var p = pointer.Native;
        var o = (options ?? new GizmoOptions()).Native;
        fixed (Mat4* m = &transform)
            return KoralNative.koral_debug_gizmo(Native, (uint)mode, (float*)m, (float*)&viewProjection, &p, &o, id).AsBool();
    }
    /// <summary>A handle is being dragged.</summary>
    public bool GizmoActive => KoralNative.koral_debug_gizmo_active(Native).AsBool();
    /// <summary>The pointer is over a handle, this frame or the last.</summary>
    public bool GizmoHovered => KoralNative.koral_debug_gizmo_hovered(Native).AsBool();

    /// <summary>Everything being drawn, now.</summary>
    public void Clear() => KoralNative.koral_debug_clear(Native);
    public ulong LineCount => KoralNative.koral_debug_line_count(Native);
    public ulong TriangleCount => KoralNative.koral_debug_triangle_count(Native);
}

/// <summary>kor::View: a scene drawn a second time, into an image of its own, by a graph of its own.</summary>
public sealed unsafe class View
{
    internal readonly IntPtr Native;
    internal View(IntPtr native) => Native = native;

    public string Name => KoralNative.Text(KoralNative.koral_view_name(Native));
    public FrameGraph Graph => new(KoralNative.koral_view_graph(Native));
    public Window Target => new(KoralNative.koral_view_target(Native));
    public Image? Image => Resource.Wrap<Image>(KoralNative.koral_view_image(Native));
    public void Resize(UVec2 extent) => KoralNative.koral_view_resize(Native, extent.X, extent.Y);
    public bool Enabled => KoralNative.koral_view_enabled(Native).AsBool();
    public void SetEnabled(bool enabled) => KoralNative.koral_view_set_enabled(Native, KoralNative.Bool(enabled));
}

/// <summary>kor::AppSettings.</summary>
public sealed record AppSettings
{
    public API Api { get; init; } = API.eVulkan;
    public WindowPlatform Platform { get; init; } = WindowPlatform.eAuto;
    public uint FramesInFlight { get; init; } = 2;
    /// <summary>A GPU to prefer by name; empty lets Koral choose.</summary>
    public string Gpu { get; init; } = "";
    /// <summary>Features of the GPU it cannot run without: the device is made with them, or refused naming them.</summary>
    public Feature RequiredFeatures { get; init; }
    /// <summary>Features it uses where the GPU has them: Context.Supports says which.</summary>
    public Feature OptionalFeatures { get; init; }
}

/// <summary>kor::WindowSettings.</summary>
public sealed record WindowSettings
{
    public string Title { get; init; } = "Koral";
    public UVec2 Extent { get; init; } = new(1280, 720);
    public bool Resizable { get; init; } = true;
    public bool Fullscreen { get; init; }
    public bool Decorated { get; init; } = true;
    public bool TransparentFramebuffer { get; init; }
    public bool Vsync { get; init; } = true;
    /// <summary>What the window presents in, most wanted first.</summary>
    public IReadOnlyList<Window.Format> Formats { get; init; } = [Window.Format.eBGRA8_UNORM, Window.Format.eRGBA8_UNORM];

    internal unsafe T WithNative<T>(Func<IntPtr, T> use)
    {
        var title = KoralNative.Utf8(Title);
        var formats = Formats.Select(f => (uint)f).ToArray();
        try
        {
            fixed (uint* f = formats)
            {
                var s = new KoralWindowSettings
                {
                    title = title, resizable = KoralNative.Bool(Resizable), fullscreen = KoralNative.Bool(Fullscreen),
                    decorated = KoralNative.Bool(Decorated), transparent_framebuffer = KoralNative.Bool(TransparentFramebuffer),
                    vsync = KoralNative.Bool(Vsync), formats = f, format_count = (nuint)formats.Length,
                };
                s.extent[0] = Extent.X;
                s.extent[1] = Extent.Y;
                return use((IntPtr)(&s));
            }
        }
        finally
        {
            KoralNative.Free(title);
        }
    }

    internal static unsafe WindowSettings From(in KoralWindowSettings s)
    {
        var formats = new Window.Format[s.format_count];
        for (nuint i = 0; i < s.format_count; ++i) formats[i] = (Window.Format)s.formats[i];
        return new WindowSettings
        {
            Title = s.title == IntPtr.Zero ? "" : KoralNative.Text((byte*)s.title),
            Extent = new UVec2(s.extent[0], s.extent[1]),
            Resizable = s.resizable.AsBool(), Fullscreen = s.fullscreen.AsBool(), Decorated = s.decorated.AsBool(),
            TransparentFramebuffer = s.transparent_framebuffer.AsBool(), Vsync = s.vsync.AsBool(),
            Formats = formats,
        };
    }
}

/// <summary>kor::OffscreenSettings.</summary>
public sealed record OffscreenSettings
{
    public string Title { get; init; } = "Offscreen";
    public UVec2 Extent { get; init; } = new(1280, 720);
    public Window.Format Format { get; init; } = Window.Format.eRGBA8_UNORM;

    internal unsafe T WithNative<T>(Func<IntPtr, T> use)
    {
        var title = KoralNative.Utf8(Title);
        try
        {
            var s = new KoralOffscreenSettings { title = title, format = (uint)Format };
            s.extent[0] = Extent.X;
            s.extent[1] = Extent.Y;
            return use((IntPtr)(&s));
        }
        finally
        {
            KoralNative.Free(title);
        }
    }
}

/// <summary>kor::Context: what the device can do.</summary>
public static class Context
{
    public static bool HasDevice => KoralNative.koral_context_has_device().AsBool();
    public static bool SupportsRayTracing => KoralNative.koral_context_supports_ray_tracing().AsBool();
    public static bool SupportsAsyncCompute => KoralNative.koral_context_supports_async_compute().AsBool();
    public static bool AsyncComputeIsSeparateFamily => KoralNative.koral_context_async_compute_is_separate_family().AsBool();
    /// <summary>Whether the device was made with <paramref name="feature"/> enabled: asked for, and the GPU has it.</summary>
    public static bool Supports(Feature feature) => KoralNative.koral_context_supports_feature((ulong)feature).AsBool();
    /// <summary>Whether the GPU has <paramref name="feature"/>, enabled or not.</summary>
    public static bool GpuHas(Feature feature) => KoralNative.koral_context_gpu_has_feature((ulong)feature).AsBool();
}

/// <summary>kor::AssetPath, kor::ShaderPath and kor::AddAssetSearchPath: where a project's files are found.</summary>
public static unsafe class Paths
{
    public static string AssetPath(string relativePath) => KoralNative.Text(KoralNative.koral_asset_path(relativePath));
    public static string ShaderPath(string relativePath) => KoralNative.Text(KoralNative.koral_shader_path(relativePath));
    public static void AddAssetSearchPath(string directory, bool front = false) => KoralNative.koral_add_asset_search_path(directory, KoralNative.Bool(front));
}
