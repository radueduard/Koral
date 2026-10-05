package koral

import java.lang.foreign.Arena
import java.lang.foreign.MemorySegment
import java.lang.foreign.SegmentAllocator
import java.lang.foreign.ValueLayout
import koral.interop.KoralLayouts
import koral.interop.KoralNative

// The objects a scene has — its window, input, clock and debug lines, and its views — each a Kotlin face on
// the C++ object, valid for as long as that is (the scene's life, for all but a view). Inside a scene's code,
// Window.current, Input.current, Time.current and Debug.current are the scene's own: kor::Window:: and the rest.

private fun current(native: MemorySegment, what: String): MemorySegment {
    if (native == MemorySegment.NULL) throw KoralException("no scene is running on this thread: $what is a scene's")
    return native
}

/** kor::Window: a scene's window — an OS window, or an image of the application's. */
class Window internal constructor(internal val native: MemorySegment) {
    val shouldClose: Boolean get() = KoralNative.koral_window_should_close(native)
    fun close() = KoralNative.koral_window_close(native)
    val isOffscreen: Boolean get() = KoralNative.koral_window_is_offscreen(native)
    /** An offscreen window's image (what it draws into), or null. */
    val image: Image? get() = Resource.wrap(KoralNative.koral_window_image(native))
    fun resize(extent: UVec2) = KoralNative.koral_window_resize(native, extent.x, extent.y)
    fun resize(width: Int, height: Int) = KoralNative.koral_window_resize(native, width, height)
    val extent: UVec2 get() = twoInts({ x, y -> KoralNative.koral_window_extent(native, x, y) }, ::UVec2)
    val isPaused: Boolean get() = KoralNative.koral_window_is_paused(native)
    val isResizable: Boolean get() = KoralNative.koral_window_is_resizable(native)
    val isFullscreen: Boolean get() = KoralNative.koral_window_is_fullscreen(native)
    val isDecorated: Boolean get() = KoralNative.koral_window_is_decorated(native)
    val isVSync: Boolean get() = KoralNative.koral_window_is_vsync(native)
    val isFramebufferTransparent: Boolean get() = KoralNative.koral_window_is_framebuffer_transparent(native)
    val format: WindowFormat get() = WindowFormat.of(KoralNative.koral_window_pixel_format(native))
    fun pause() = KoralNative.koral_window_pause(native)
    fun unpause() = KoralNative.koral_window_unpause(native)
    var title: String
        get() = KoralNative.koral_window_title(native)
        set(value) = KoralNative.koral_window_set_title(native, value)
    val defaultFramebuffer: Framebuffer? get() = Resource.wrap(KoralNative.koral_window_default_framebuffer(native))
    val hasResized: Boolean get() = KoralNative.koral_window_has_resized(native)
    fun setIcon(path: String) = KoralNative.koral_window_set_icon(native, path)
    val isFocused: Boolean get() = KoralNative.koral_window_is_focused(native)
    val isShownThisFrame: Boolean get() = KoralNative.koral_window_is_shown_this_frame(native)

    companion object {
        /** The window of the scene running on this thread. */
        val current: Window get() = Window(current(KoralNative.koral_current_window(), "Window.current"))
    }
}

/**
 * kor::InputSource: a key, a mouse button, a gamepad button or a gamepad axis, scaled — what actions and
 * axes are bound to. `-InputSource(Key.eA)` counts the other way on an axis.
 */
data class InputSource(val kind: InputSourceKind, val code: Int, val scale: Float = 1f) {
    constructor(key: Key, scale: Float = 1f) : this(InputSourceKind.eKey, key.value, scale)
    constructor(button: MouseButton, scale: Float = 1f) : this(InputSourceKind.eMouseButton, button.value, scale)
    constructor(button: GamepadButton, scale: Float = 1f) : this(InputSourceKind.eGamepadButton, button.value, scale)
    constructor(axis: GamepadAxis, scale: Float = 1f) : this(InputSourceKind.eGamepadAxis, axis.value, scale)

    operator fun unaryMinus() = copy(scale = -scale)

    internal fun write(segment: MemorySegment) {
        Fields(segment, KoralLayouts.KoralInputSource).int("kind", kind.value).int("code", code).float("scale", scale)
    }

    /** Its name, as bindings files write it: "Space", "Gamepad A", "-Left Stick X". */
    val name: String get() = Arena.ofConfined().use { a -> KoralNative.koral_input_source_name(a.allocate(KoralLayouts.KoralInputSource).also(::write)) }

    override fun toString() = name

    companion object {
        /** The source a bindings name names, or null. */
        fun parse(name: String): InputSource? = Arena.ofConfined().use { a ->
            val s = a.allocate(KoralLayouts.KoralInputSource)
            if (!KoralNative.koral_input_source_parse(name, s)) return null
            val f = Fields(s, KoralLayouts.KoralInputSource)
            InputSource(InputSourceKind.of(f.readInt("kind")), f.readInt("code"), f.readFloat("scale"))
        }
    }
}

/** Every action and axis an input is bound to, by source name: what a settings screen shows and rebinds. */
data class InputBindings(val actions: List<Entry> = emptyList(), val axes: List<Entry> = emptyList()) {
    data class Entry(val name: String, val sources: List<String>)

    internal fun toJson(): String = Json.write(mapOf(
        "actions" to actions.map { mapOf("name" to it.name, "sources" to it.sources) },
        "axes" to axes.map { mapOf("name" to it.name, "sources" to it.sources) }))

    internal companion object {
        @Suppress("UNCHECKED_CAST")
        fun fromJson(json: String): InputBindings {
            val root = Json.parse(json) as? Map<String, Any?> ?: return InputBindings()
            fun entries(key: String) = (root[key] as? List<Map<String, Any?>>).orEmpty().map {
                Entry(it["name"] as? String ?: "", (it["sources"] as? List<String>).orEmpty())
            }
            return InputBindings(entries("actions"), entries("axes"))
        }
    }
}

/** kor::Input: a scene's keyboard, mouse and gamepads, as they were this frame — and its actions and axes. */
class Input internal constructor(internal val native: MemorySegment) {
    fun stateOf(key: Key): KeyState = KeyState.of(KoralNative.koral_input_state_of(native, key.value))
    fun mouseButtonState(button: MouseButton): KeyState = KeyState.of(KoralNative.koral_input_mouse_button_state(native, button.value))
    fun isKeyPressed(key: Key) = stateOf(key) == KeyState.ePressed
    fun isKeyHeld(key: Key) = stateOf(key) == KeyState.eHeld
    fun isKeyReleased(key: Key) = stateOf(key) == KeyState.eReleased
    fun isKeyRepeated(key: Key) = KoralNative.koral_input_is_key_repeated(native, key.value)
    fun isMouseButtonPressed(button: MouseButton) = mouseButtonState(button) == KeyState.ePressed
    fun isMouseButtonHeld(button: MouseButton) = mouseButtonState(button) == KeyState.eHeld
    fun isMouseButtonReleased(button: MouseButton) = mouseButtonState(button) == KeyState.eReleased

    fun firstKeyPressed(): Key? = Arena.ofConfined().use { a ->
        val out = a.allocate(ValueLayout.JAVA_INT)
        if (KoralNative.koral_input_first_key_pressed(native, out)) Key.of(out.get(ValueLayout.JAVA_INT, 0)) else null
    }
    fun firstMouseButtonPressed(): MouseButton? = Arena.ofConfined().use { a ->
        val out = a.allocate(ValueLayout.JAVA_INT)
        if (KoralNative.koral_input_first_mouse_button_pressed(native, out)) MouseButton.of(out.get(ValueLayout.JAVA_INT, 0)) else null
    }

    /** Whether an interface over the scene (koral-ui, say) is using the pointer: a camera should stay put. */
    val interfaceWantsMouse: Boolean get() = KoralNative.koral_input_interface_wants_mouse(native)
    val interfaceWantsKeyboard: Boolean get() = KoralNative.koral_input_interface_wants_keyboard(native)
    val mousePosition: Vec2 get() = twoFloats({ x, y -> KoralNative.koral_input_mouse_position(native, x, y) }, ::Vec2)
    val mousePositionDelta: Vec2 get() = twoFloats({ x, y -> KoralNative.koral_input_mouse_position_delta(native, x, y) }, ::Vec2)
    val mouseScrollDelta: Vec2 get() = twoFloats({ x, y -> KoralNative.koral_input_mouse_scroll_delta(native, x, y) }, ::Vec2)
    val lastMousePosition: Vec2 get() = twoFloats({ x, y -> KoralNative.koral_input_last_mouse_position(native, x, y) }, ::Vec2)
    /** The text typed this frame: what a text field inserts. */
    val typedText: String get() = KoralNative.koral_input_typed_text(native)

    /** The cursor: shown, hidden, or captured (hidden, and moving without limit — a first-person camera). */
    var cursorMode: InputCursorMode
        get() = InputCursorMode.of(KoralNative.koral_input_current_cursor_mode(native))
        set(value) = KoralNative.koral_input_set_cursor_mode(native, value.value)

    fun isGamepadConnected(pad: Int = 0) = KoralNative.koral_input_is_gamepad_connected(native, pad)
    fun gamepadName(pad: Int = 0): String = KoralNative.koral_input_gamepad_name(native, pad)
    fun gamepadButtonState(button: GamepadButton, pad: Int = 0): KeyState = KeyState.of(KoralNative.koral_input_gamepad_button_state(native, button.value, pad))
    fun isGamepadButtonPressed(button: GamepadButton, pad: Int = 0) = gamepadButtonState(button, pad) == KeyState.ePressed
    fun isGamepadButtonHeld(button: GamepadButton, pad: Int = 0) = gamepadButtonState(button, pad) == KeyState.eHeld
    fun isGamepadButtonReleased(button: GamepadButton, pad: Int = 0) = gamepadButtonState(button, pad) == KeyState.eReleased
    fun gamepadAxisValue(axis: GamepadAxis, pad: Int = 0): Float = KoralNative.koral_input_gamepad_axis_value(native, axis.value, pad)
    fun setGamepadDeadZone(deadZone: Float) = KoralNative.koral_input_set_gamepad_dead_zone(native, deadZone)

    // ---- actions and axes ---------------------------------------------------------------------------------

    /** BindAction: [action] is down while any of [sources] is. */
    fun bindAction(action: String, vararg sources: InputSource) = Arena.ofConfined().use { a ->
        KoralNative.koral_input_bind_action(native, action, sources(a, sources), sources.size.toLong())
    }
    /** BindAxis: [axis] is the sum of [sources], each scaled — `bindAxis("MoveX", InputSource(Key.eD), -InputSource(Key.eA))`. */
    fun bindAxis(axis: String, vararg sources: InputSource) = Arena.ofConfined().use { a ->
        KoralNative.koral_input_bind_axis(native, axis, sources(a, sources), sources.size.toLong())
    }
    fun bindAction(action: String, vararg keys: Key) = bindAction(action, *keys.map { InputSource(it) }.toTypedArray())

    private fun sources(a: SegmentAllocator, sources: Array<out InputSource>): MemorySegment {
        val s = a.allocate(KoralLayouts.KoralInputSource, maxOf(1, sources.size).toLong())
        sources.forEachIndexed { i, src -> src.write(s.asSlice(i * KoralLayouts.KoralInputSource.byteSize())) }
        return s
    }

    fun actionState(action: String): KeyState = KeyState.of(KoralNative.koral_input_action_state(native, action))
    fun isActionPressed(action: String) = actionState(action) == KeyState.ePressed
    fun isActionHeld(action: String) = actionState(action) == KeyState.eHeld
    fun isActionReleased(action: String) = actionState(action) == KeyState.eReleased
    fun axis(axis: String): Float = KoralNative.koral_input_axis(native, axis)
    /** Two axes as a vector, its length at most 1 — a diagonal is no faster. */
    fun axis2D(x: String, y: String): Vec2 = twoFloats({ ox, oy -> KoralNative.koral_input_axis_2d(native, x, y, ox, oy) }, ::Vec2)

    /** Every binding, by source name; set it back to rebind. */
    var bindings: InputBindings
        get() = InputBindings.fromJson(KoralNative.koral_input_bindings(native))
        set(value) = checked(KoralNative.koral_input_set_bindings(native, value.toJson()), "setting the bindings")

    // ---- what an offscreen scene is given, as a window's events would give it --------------------------------

    fun feedKey(key: Key, down: Boolean) = KoralNative.koral_input_feed_key(native, key.value, down)
    fun feedMouseButton(button: MouseButton, down: Boolean) = KoralNative.koral_input_feed_mouse_button(native, button.value, down)
    fun feedMousePosition(position: Vec2) = KoralNative.koral_input_feed_mouse_position(native, position.x, position.y)
    fun feedMouseDelta(delta: Vec2) = KoralNative.koral_input_feed_mouse_delta(native, delta.x, delta.y)
    fun feedScroll(delta: Vec2) = KoralNative.koral_input_feed_scroll(native, delta.x, delta.y)
    fun feedText(text: String) = KoralNative.koral_input_feed_text(native, text)
    fun feedKeyRepeat(key: Key) = KoralNative.koral_input_feed_key_repeat(native, key.value)
    fun feedGamepadButton(button: GamepadButton, down: Boolean, pad: Int = 0) = KoralNative.koral_input_feed_gamepad_button(native, button.value, down, pad)
    fun feedGamepadAxis(axis: GamepadAxis, value: Float, pad: Int = 0) = KoralNative.koral_input_feed_gamepad_axis(native, axis.value, value, pad)
    /** Releases every key and button: what losing focus does. */
    fun releaseAll() = KoralNative.koral_input_release_all(native)

    companion object {
        const val MaxGamepads = 4
        /** The input of the scene running on this thread. */
        val current: Input get() = Input(current(KoralNative.koral_current_input(), "Input.current"))
        fun describe(key: Key): String = KoralNative.koral_input_describe_key(key.value)
        fun describe(button: MouseButton): String = KoralNative.koral_input_describe_mouse_button(button.value)
    }
}

/** kor::Time: a scene's clock. */
class Time internal constructor(internal val native: MemorySegment) {
    /** Seconds since the last frame, scaled by [timeScale]. */
    val frameTime: Float get() = KoralNative.koral_time_frame_time(native)
    val unscaledFrameTime: Float get() = KoralNative.koral_time_unscaled_frame_time(native)
    /** The step fixedUpdate advances by, in seconds. */
    var fixedDeltaTime: Float
        get() = KoralNative.koral_time_fixed_delta_time(native)
        set(value) = KoralNative.koral_time_set_fixed_delta_time(native, value)
    /** How far between two fixed steps this frame is: what interpolating a fixed-step simulation uses. */
    val fixedStepFraction: Float get() = KoralNative.koral_time_fixed_step_fraction(native)
    val inFixedStep: Boolean get() = KoralNative.koral_time_in_fixed_step(native)
    val elapsed: Float get() = KoralNative.koral_time_elapsed(native)
    val frameCount: Long get() = KoralNative.koral_time_frame_count(native)
    var timeScale: Float
        get() = KoralNative.koral_time_time_scale(native)
        set(value) = KoralNative.koral_time_set_time_scale(native, value)

    companion object {
        const val MaxFixedSteps = 8
        /** The clock of the scene running on this thread. */
        val current: Time get() = Time(current(KoralNative.koral_current_time(), "Time.current"))
    }
}

/**
 * kor::DebugStyle: a debug shape's outline colour, how long it stays (0: this frame), whether it shows through, the
 * colour it is filled with (alpha below 1 is see-through, 0 no fill), whether it has its outline, and how wide its
 * lines are, in pixels.
 */
data class DebugStyle(val color: Vec4 = Vec4.One, val duration: Float = 0f, val onTop: Boolean = false,
                      val fill: Vec4 = Vec4.Zero, val outline: Boolean = true, val lineWidth: Float = 1f) {
    internal fun native(a: SegmentAllocator) = Fields(a, KoralLayouts.KoralDebugStyle)
        .floats("color", *color.toArray()).float("duration", duration).bool("on_top", onTop)
        .floats("fill", *fill.toArray()).bool("fill_only", !outline).float("line_width", lineWidth).segment
}

/**
 * kor::GizmoPointer: the pointer a gizmo is used with, in the pixels of the image its camera draws — [position] null
 * when it is not over the image (or is something else's).
 */
data class GizmoPointer(val position: Vec2?, val viewport: Vec2, val down: Boolean, val pressed: Boolean = false) {
    internal fun native(a: SegmentAllocator) = Fields(a, KoralLayouts.KoralGizmoPointer)
        .floats("position", position?.x ?: 0f, position?.y ?: 0f).bool("has_position", position != null)
        .floats("viewport", viewport.x, viewport.y).bool("down", down).bool("pressed", pressed).segment
}

/** kor::GizmoOptions: world or local axes, its size on screen in pixels, and the steps it snaps to (0: none). */
data class GizmoOptions(val space: GizmoSpace = GizmoSpace.eWorld, val size: Float = 100f, val snap: Float = 0f) {
    internal fun native(a: SegmentAllocator) = Fields(a, KoralLayouts.KoralGizmoOptions)
        .int("space", space.value).float("size", size).float("snap", snap).segment
}

/** kor::DebugDraw: lines for seeing what code does — drawn by a DebugDrawPass, gone after their duration. */
class DebugDraw internal constructor(internal val native: MemorySegment) {
    private inline fun draw(style: DebugStyle?, body: (Arena, MemorySegment) -> Unit) =
        Arena.ofConfined().use { a -> body(a, (style ?: DebugStyle()).native(a)) }
    private fun Arena.v3(v: Vec3) = allocateFrom(ValueLayout.JAVA_FLOAT, v.x, v.y, v.z)
    private fun Arena.m4(m: Mat4) = allocateFrom(ValueLayout.JAVA_FLOAT, *m.toArray())

    fun line(from: Vec3, to: Vec3, style: DebugStyle? = null) = draw(style) { a, s -> KoralNative.koral_debug_line(native, a.v3(from), a.v3(to), s) }
    fun box(min: Vec3, max: Vec3, style: DebugStyle? = null) = draw(style) { a, s -> KoralNative.koral_debug_box(native, a.v3(min), a.v3(max), s) }
    fun box(transform: Mat4, style: DebugStyle? = null) = draw(style) { a, s -> KoralNative.koral_debug_box_transform(native, a.m4(transform), s) }
    fun circle(center: Vec3, normal: Vec3, radius: Float, style: DebugStyle? = null, segments: Int = 32) =
        draw(style) { a, s -> KoralNative.koral_debug_circle(native, a.v3(center), a.v3(normal), radius, s, segments) }
    fun sphere(center: Vec3, radius: Float, style: DebugStyle? = null, segments: Int = 32) =
        draw(style) { a, s -> KoralNative.koral_debug_sphere(native, a.v3(center), radius, s, segments) }
    fun arrow(from: Vec3, to: Vec3, style: DebugStyle? = null) = draw(style) { a, s -> KoralNative.koral_debug_arrow(native, a.v3(from), a.v3(to), s) }
    fun point(position: Vec3, size: Float = 0.1f, style: DebugStyle? = null) = draw(style) { a, s -> KoralNative.koral_debug_point(native, a.v3(position), size, s) }
    fun axes(transform: Mat4, size: Float = 1f, duration: Float = 0f) = Arena.ofConfined().use { a -> KoralNative.koral_debug_axes(native, a.m4(transform), size, duration) }
    fun grid(center: Vec3, size: Float, cells: Int, style: DebugStyle? = null) = draw(style) { a, s -> KoralNative.koral_debug_grid(native, a.v3(center), size, cells, s) }
    fun frustum(viewProjection: Mat4, style: DebugStyle? = null) = draw(style) { a, s -> KoralNative.koral_debug_frustum(native, a.m4(viewProjection), s) }
    fun triangle(a: Vec3, b: Vec3, c: Vec3, style: DebugStyle? = null) = draw(style) { m, s -> KoralNative.koral_debug_triangle(native, m.v3(a), m.v3(b), m.v3(c), s) }
    /** Four corners, in order around the edge. */
    fun quad(a: Vec3, b: Vec3, c: Vec3, d: Vec3, style: DebugStyle? = null) =
        draw(style) { m, s -> KoralNative.koral_debug_quad(native, m.v3(a), m.v3(b), m.v3(c), m.v3(d), s) }
    fun plane(center: Vec3, normal: Vec3, size: Vec2, style: DebugStyle? = null) =
        draw(style) { m, s -> KoralNative.koral_debug_plane(native, m.v3(center), m.v3(normal), m.allocateFrom(ValueLayout.JAVA_FLOAT, size.x, size.y), s) }
    fun cylinder(from: Vec3, to: Vec3, radius: Float, style: DebugStyle? = null, segments: Int = 24) =
        draw(style) { m, s -> KoralNative.koral_debug_cylinder(native, m.v3(from), m.v3(to), radius, s, segments) }
    /** A cone with its base's centre at [base] and its point at [tip]. */
    fun cone(base: Vec3, tip: Vec3, radius: Float, style: DebugStyle? = null, segments: Int = 24) =
        draw(style) { m, s -> KoralNative.koral_debug_cone(native, m.v3(base), m.v3(tip), radius, s, segments) }
    fun capsule(from: Vec3, to: Vec3, radius: Float, style: DebugStyle? = null, segments: Int = 24) =
        draw(style) { m, s -> KoralNative.koral_debug_capsule(native, m.v3(from), m.v3(to), radius, s, segments) }
    /** A camera: the pyramid it sees through, [size] deep, with a triangle on top for up. */
    fun camera(view: Mat4, projection: Mat4, size: Float = 1f, style: DebugStyle? = null) =
        draw(style) { m, s -> KoralNative.koral_debug_camera(native, m.m4(view), m.m4(projection), size, s) }
    /** A star where it is, and the sphere it reaches to (none when [range] is 0: no limit). */
    fun pointLight(position: Vec3, range: Float, style: DebugStyle? = null) =
        draw(style) { m, s -> KoralNative.koral_debug_point_light(native, m.v3(position), range, s) }
    /** The cone it lights; angles from the centre to the edge, in radians. */
    fun spotLight(position: Vec3, direction: Vec3, range: Float, outerAngle: Float, innerAngle: Float = 0f, style: DebugStyle? = null) =
        draw(style) { m, s -> KoralNative.koral_debug_spot_light(native, m.v3(position), m.v3(direction), range, outerAngle, innerAngle, s) }
    /** The sun: a disc at [position] with its rays. */
    fun directionalLight(position: Vec3, direction: Vec3, size: Float = 1f, style: DebugStyle? = null) =
        draw(style) { m, s -> KoralNative.koral_debug_directional_light(native, m.v3(position), m.v3(direction), size, s) }

    /**
     * kor::DebugDraw::Gizmo: handles on [transform], the one under [pointer] dragged while its button is down. Call it
     * every frame the thing is selected. Returns the moved transform, or null when it did not move.
     */
    fun gizmo(mode: GizmoMode, transform: Mat4, viewProjection: Mat4, pointer: GizmoPointer,
              options: GizmoOptions = GizmoOptions(), id: Long = 0): Mat4? = Arena.ofConfined().use { a ->
        val m = a.m4(transform)
        val changed = KoralNative.koral_debug_gizmo(native, mode.value, m, a.m4(viewProjection), pointer.native(a), options.native(a), id)
        if (changed) Mat4(m.toArray(ValueLayout.JAVA_FLOAT)) else null
    }
    /** A handle is being dragged. */
    val gizmoActive: Boolean get() = KoralNative.koral_debug_gizmo_active(native)
    /** The pointer is over a handle, this frame or the last. */
    val gizmoHovered: Boolean get() = KoralNative.koral_debug_gizmo_hovered(native)

    fun clear() = KoralNative.koral_debug_clear(native)
    val lineCount: Long get() = KoralNative.koral_debug_line_count(native)
    val triangleCount: Long get() = KoralNative.koral_debug_triangle_count(native)
}

/** The debug lines of the scene running on this thread: kor::Debug::. */
object Debug {
    val current: DebugDraw get() = DebugDraw(current(KoralNative.koral_current_debug(), "Debug.current"))

    /** kor::Scene::Debug::Gizmo: a gizmo used with the scene's own mouse (its left button) over its window. */
    fun gizmo(mode: GizmoMode, transform: Mat4, viewProjection: Mat4, options: GizmoOptions = GizmoOptions(), id: Long = 0): Mat4? {
        current   // the scene's, or the reason there is none
        return Arena.ofConfined().use { a ->
            val m = a.allocateFrom(ValueLayout.JAVA_FLOAT, *transform.toArray())
            val changed = KoralNative.koral_current_gizmo(mode.value, m, a.allocateFrom(ValueLayout.JAVA_FLOAT, *viewProjection.toArray()),
                                                          options.native(a), id)
            if (changed) Mat4(m.toArray(ValueLayout.JAVA_FLOAT)) else null
        }
    }
}

/**
 * kor::View: a scene's second frame graph, drawing into an image of its own — a minimap, a preview — that the
 * scene's main graph (or an interface) shows.
 */
class View internal constructor(internal val native: MemorySegment) {
    val name: String get() = KoralNative.koral_view_name(native)
    val graph: FrameGraph get() = FrameGraph(KoralNative.koral_view_graph(native))
    val target: Window get() = Window(KoralNative.koral_view_target(native))
    val image: Image? get() = Resource.wrap(KoralNative.koral_view_image(native))
    fun resize(extent: UVec2) = KoralNative.koral_view_resize(native, extent.x, extent.y)
    var enabled: Boolean
        get() = KoralNative.koral_view_enabled(native)
        set(value) = KoralNative.koral_view_set_enabled(native, value)
}
