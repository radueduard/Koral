package koral

import java.lang.foreign.Arena
import java.lang.foreign.FunctionDescriptor
import java.lang.foreign.MemorySegment
import java.lang.foreign.ValueLayout
import java.lang.foreign.ValueLayout.ADDRESS
import java.lang.foreign.ValueLayout.JAVA_BOOLEAN
import java.lang.foreign.ValueLayout.JAVA_INT
import java.lang.invoke.MethodHandles
import java.lang.ref.WeakReference
import java.util.concurrent.ConcurrentHashMap
import java.util.concurrent.CopyOnWriteArrayList
import koral.interop.KoralLayouts
import koral.interop.KoralNative
import koral.interop.Native

/** kor::AppSettings. */
data class AppSettings(
    val api: API = API.eVulkan,
    val platform: WindowPlatform = WindowPlatform.eAuto,
    val framesInFlight: Int? = null,
    val gpu: String? = null,
)

/** kor::WindowSettings. */
data class WindowSettings(
    val title: String = "Koral",
    val width: Int = 1280,
    val height: Int = 720,
    val resizable: Boolean = true,
    val fullscreen: Boolean = false,
    val decorated: Boolean = true,
    val transparentFramebuffer: Boolean = false,
    val vsync: Boolean = true,
    /** The surface formats it would like, best first: the first the display supports is used. */
    val formats: List<WindowFormat> = listOf(WindowFormat.eBGRA8_UNORM, WindowFormat.eRGBA8_UNORM),
) {
    internal fun native(a: Arena): MemorySegment {
        val s = KoralNative.koral_window_settings_default(a)
        val f = a.allocate(JAVA_INT, maxOf(1, formats.size).toLong())
        formats.forEachIndexed { i, format -> f.setAtIndex(JAVA_INT, i.toLong(), format.value) }
        Fields(s, KoralLayouts.KoralWindowSettings).string(a, "title", title).ints("extent", width, height)
            .bool("resizable", resizable).bool("fullscreen", fullscreen).bool("decorated", decorated)
            .bool("transparent_framebuffer", transparentFramebuffer).bool("vsync", vsync)
            .address("formats", f).long("format_count", formats.size.toLong())
        return s
    }

    internal companion object {
        private fun offset(field: String) = KoralLayouts.KoralWindowSettings.byteOffset(java.lang.foreign.MemoryLayout.PathElement.groupElement(field))

        fun from(s: MemorySegment): WindowSettings {
            val f = Fields(s, KoralLayouts.KoralWindowSettings)
            val count = s.get(ValueLayout.JAVA_LONG, offset("format_count"))
            val list = s.get(ADDRESS, offset("formats")).reinterpret(count * 4)
            val title = s.get(ADDRESS, offset("title"))
            return WindowSettings(
                title = if (title == MemorySegment.NULL) "Koral" else Native.kString(title),
                width = f.readInt("extent", 0), height = f.readInt("extent", 1),
                resizable = f.readBool("resizable"), fullscreen = f.readBool("fullscreen"), decorated = f.readBool("decorated"),
                transparentFramebuffer = f.readBool("transparent_framebuffer"), vsync = f.readBool("vsync"),
                formats = List(count.toInt()) { WindowFormat.of(list.getAtIndex(JAVA_INT, it.toLong())) })
        }
    }
}

/** kor::OffscreenSettings: a scene drawing into an image of the application's, with no OS window. */
data class OffscreenSettings(
    val title: String = "Offscreen",
    val width: Int = 1280,
    val height: Int = 720,
    val format: WindowFormat = WindowFormat.eRGBA8_UNORM,
) {
    internal fun native(a: Arena): MemorySegment {
        val s = KoralNative.koral_offscreen_settings_default(a)
        Fields(s, KoralLayouts.KoralOffscreenSettings).string(a, "title", title).ints("extent", width, height).int("format", format.value)
        return s
    }
}

/**
 * kor::SceneArgs: what a scene is opened with — from koral.json, `Navigator.push`, or the command line —
 * as strings by name, read as the type wanted.
 */
class SceneArgs(values: Map<String, String> = emptyMap()) {
    private val values = LinkedHashMap(values)
    val entries: Map<String, String> get() = values

    fun with(key: String, value: Any) = SceneArgs(values + (key to value.toString()))
    operator fun contains(key: String) = key in values
    fun string(key: String, fallback: String = "") = values[key] ?: fallback
    fun number(key: String, fallback: Double = 0.0) = values[key]?.toDoubleOrNull() ?: fallback
    fun integer(key: String, fallback: Long = 0) = values[key]?.let { it.toLongOrNull() ?: it.toDoubleOrNull()?.toLong() } ?: fallback
    fun flag(key: String, fallback: Boolean = false) =
        values[key]?.lowercase()?.let { it == "true" || it == "1" || it == "yes" || it == "on" } ?: fallback

    fun toJson(): String = Json.write(values)

    companion object {
        /** A JSON object's members, as strings (a number or a flag as it was written). */
        fun fromJson(json: String?): SceneArgs {
            if (json.isNullOrBlank()) return SceneArgs()
            val parsed = runCatching { Json.parse(json) }.getOrNull() as? Map<*, *> ?: return SceneArgs()
            return SceneArgs(parsed.entries.associate { (k, v) ->
                k.toString() to when (v) {
                    null -> ""
                    is String -> v
                    is Double -> if (v % 1.0 == 0.0) v.toLong().toString() else v.toString()
                    is Map<*, *>, is List<*> -> Json.write(v)
                    else -> v.toString()
                }
            })
        }
    }
}

/**
 * A library that brings a native Koral module (koral-ui's) with it. Listed in
 * `META-INF/services/koral.NativeModule`, it is loaded when an [App] is made, before Koral starts — a module
 * that arrives after that never has its startup and shutdown hooks run.
 */
interface NativeModule {
    fun load()
}

/** Names a scene class, for [App.register]: `@SceneName("Menu") class MenuScene : Scene()`. */
@Target(AnnotationTarget.CLASS)
annotation class SceneName(val name: String)

/**
 * kor::App: the application — the device, and the windows its scenes are shown in. One per process; close
 * it last (`use { }`), which closes what it owns first.
 *
 * ```
 * App(AppSettings()).use { app ->
 *     app.register("Menu") { Menu() }
 *     app.open("Menu", WindowSettings(title = "Menu"))
 *     app.run()
 * }
 * ```
 */
class App(val settings: AppSettings = AppSettings()) : Owner(), AutoCloseable {
    init {
        check(current == null) { "an application already exists: one per process" }
        // A module's library registers it when it loads, and its hooks run only if that was before startup.
        java.util.ServiceLoader.load(NativeModule::class.java, App::class.java.classLoader).forEach { it.load() }
        Arena.ofConfined().use { a ->
            val s = KoralNative.koral_app_settings_default(a)
            Fields(s, KoralLayouts.KoralAppSettings).apply {
                int("api", settings.api.value)
                int("platform", settings.platform.value)
                settings.framesInFlight?.let { int("frames_in_flight", it) }
                settings.gpu?.let { string(a, "gpu", it) }
            }
            checked(KoralNative.koral_app_create(s), "creating the application")
        }
        current = this
        HotReload.startIfRequested()
    }

    // ---- scenes by name -------------------------------------------------------------------------------

    /** Registers a scene by name: what open, Navigator and koral.json name it by. */
    fun register(name: String, factory: (SceneArgs) -> Scene) {
        checked(KoralNative.koral_app_register(name, SceneBridge.factoryStub, Handles.put(factory)), "registering $name")
    }

    /** Registers [S] by its [SceneName] (or its class name), made with its (SceneArgs) or () constructor. */
    inline fun <reified S : Scene> register(name: String? = null) = register(S::class.java, name)

    fun register(type: Class<out Scene>, name: String? = null) {
        val withArgs = type.constructors.firstOrNull { it.parameterCount == 1 && it.parameterTypes[0] == SceneArgs::class.java }
        val plain = type.constructors.firstOrNull { it.parameterCount == 0 }
        require(withArgs != null || plain != null) { "${type.simpleName} needs a public constructor taking SceneArgs, or none" }
        register(name ?: sceneNameOf(type)) { args -> (withArgs?.newInstance(args) ?: plain!!.newInstance()) as Scene }
    }

    /** Loads a scene library (one exporting KORAL_SCENES); returns the scene names it added. */
    fun loadLibrary(path: String): List<String> {
        val before = sceneNames.toSet()
        checked(KoralNative.koral_app_load_library(path), "loading $path")
        return sceneNames.filter { it !in before }
    }
    fun unloadLibrary(path: String) = checked(KoralNative.koral_app_unload_library(path), "unloading $path")
    fun reloadLibrary(path: String) = checked(KoralNative.koral_app_reload_library(path), "reloading $path")

    /** Closes the scenes named and opens them again, handing what their saveState gave to their loadState. */
    fun reloadScenes(vararg names: String) = Arena.ofConfined().use { a ->
        val list = a.allocate(ADDRESS, maxOf(1, names.size).toLong())
        names.forEachIndexed { i, n -> list.setAtIndex(ADDRESS, i.toLong(), a.allocateFrom(n)) }
        checked(KoralNative.koral_app_reload_scenes(list, names.size.toLong()), "reloading scenes")
    }

    val sceneNames: List<String> get() = List(KoralNative.koral_app_scene_name_count()) { KoralNative.koral_app_scene_name(it) }

    // ---- opening --------------------------------------------------------------------------------------

    fun open(name: String, window: WindowSettings = WindowSettings(), args: SceneArgs = SceneArgs()): Scene =
        Arena.ofConfined().use { a -> SceneBridge.of(checked(KoralNative.koral_app_open(name, window.native(a), args.toJson()), "opening $name")) }

    /** Opens [scene], made here, in a window. */
    fun <S : Scene> open(name: String, scene: S, window: WindowSettings = WindowSettings()): S = Arena.ofConfined().use { a ->
        val callbacks = a.allocate(KoralLayouts.KoralSceneCallbacks)
        SceneBridge.fill(scene, callbacks)
        checked(KoralNative.koral_app_open_scene(name, callbacks, window.native(a)), "opening $name")
        scene
    }

    fun openOffscreen(name: String, target: OffscreenSettings = OffscreenSettings(), args: SceneArgs = SceneArgs()): Scene =
        Arena.ofConfined().use { a -> SceneBridge.of(checked(KoralNative.koral_app_open_offscreen(name, target.native(a), args.toJson()), "opening $name")) }

    /** Opens [scene], made here, drawing into an image. */
    fun <S : Scene> openOffscreen(name: String, scene: S, target: OffscreenSettings = OffscreenSettings()): S = Arena.ofConfined().use { a ->
        val callbacks = a.allocate(KoralLayouts.KoralSceneCallbacks)
        SceneBridge.fill(scene, callbacks)
        checked(KoralNative.koral_app_open_offscreen_scene(name, callbacks, target.native(a)), "opening $name")
        scene
    }

    // ---- shared ---------------------------------------------------------------------------------------

    private val shared = HashMap<String, WeakReference<Any>>()

    /**
     * App::Shared: one object for every scene that asks for [key], made by [make] for the first — kept while
     * any scene still holds it.
     */
    @Suppress("UNCHECKED_CAST")
    fun <T : Any> shared(key: String, make: () -> T): T = synchronized(shared) {
        (shared[key]?.get() as T?) ?: make().also { shared[key] = WeakReference(it) }
    }
    fun isShared(key: String): Boolean = synchronized(shared) { shared[key]?.get() != null }

    // ---- running --------------------------------------------------------------------------------------

    /** The scene each window shows, in the order the windows were opened. */
    val scenes: List<Scene>
        get() = Arena.ofConfined().use { a ->
            val count = KoralNative.koral_app_scenes(MemorySegment.NULL, 0).toInt()
            val out = a.allocate(ADDRESS, maxOf(1, count).toLong())
            val got = minOf(count.toLong(), KoralNative.koral_app_scenes(out, count.toLong())).toInt()
            List(got) { SceneBridge.live[out.getAtIndex(ADDRESS, it.toLong()).address()] }.filterNotNull()
        }

    fun findScene(name: String): Scene? = scenes.firstOrNull { it.name == name }

    /**
     * One frame of every scene. Before it, between frames: edits a hot reload applies, and coroutines
     * whose wait is over resume. False once no scene is left.
     */
    fun frame(): Boolean {
        HotReload.poll()
        MainThread.resume()
        val running = KoralNative.koral_app_frame()
        checkLastError()
        return running
    }

    /** Frames until no scene is left, or quit. */
    fun run(): Int {
        while (frame()) { /* the frame is the work */ }
        return 0
    }

    fun quit() = KoralNative.koral_app_quit()
    fun replace(shown: Scene, name: String, args: SceneArgs = SceneArgs()) = KoralNative.koral_app_replace(shown.native, name, args.toJson())
    fun push(over: Scene, name: String, args: SceneArgs = SceneArgs()) = KoralNative.koral_app_push(over.native, name, args.toJson())
    fun pop(shown: Scene) = KoralNative.koral_app_pop(shown.native)
    fun close(shown: Scene) = KoralNative.koral_app_close(shown.native)

    /** Told when a scene's hook throws: the scene, the hook, the exception. (It is logged either way.) */
    fun onHookFailed(listener: (Scene, String, Throwable) -> Unit) { hookFailed += listener }

    override fun close() {
        if (current !== this) return
        closeOwned()
        KoralNative.koral_app_destroy()
        hookFailed.clear()
        current = null
    }

    companion object {
        /** The application, once made. */
        var current: App? = null
            private set

        internal val hookFailed = CopyOnWriteArrayList<(Scene, String, Throwable) -> Unit>()

        fun sceneNameOf(type: Class<out Scene>): String = type.getAnnotation(SceneName::class.java)?.name ?: type.simpleName

        /**
         * A program's whole main, as the C++ runtime and koral-dotnet run a project: reads koral.json (from the
         * working directory up) and [args] (`--platform`, `--scene`, …), makes the application, lets [setup]
         * register the scenes, opens the project's scene — or the first registered — and runs until it closes.
         *
         * ```
         * fun main(args: Array<String>) = App.launch(args) { register<Orbit>() }
         * ```
         */
        fun launch(args: Array<String>, setup: App.() -> Unit): Unit = ProjectConfig.load(args.toList()).use { project ->
            App(project.appSettings).use { app ->
                app.setup()
                val name = project.scene.ifEmpty { app.sceneNames.firstOrNull() ?: throw KoralException("no scene is registered") }
                app.open(name, project.windowSettings)
                app.run()
            }
        }
    }
}

/** kor::Navigator: the current scene's own window, from inside it. */
object Navigator {
    fun open(name: String, window: WindowSettings = WindowSettings(), args: SceneArgs = SceneArgs()): Scene =
        Arena.ofConfined().use { a -> SceneBridge.of(checked(KoralNative.koral_navigator_open(name, window.native(a), args.toJson()), "opening $name")) }
    fun openOffscreen(name: String, target: OffscreenSettings = OffscreenSettings(), args: SceneArgs = SceneArgs()): Scene =
        Arena.ofConfined().use { a -> SceneBridge.of(checked(KoralNative.koral_navigator_open_offscreen(name, target.native(a), args.toJson()), "opening $name")) }
    fun replace(name: String, args: SceneArgs = SceneArgs()) = KoralNative.koral_navigator_replace(name, args.toJson())
    fun push(name: String, args: SceneArgs = SceneArgs()) = KoralNative.koral_navigator_push(name, args.toJson())
    fun pop() = KoralNative.koral_navigator_pop()
    fun close() = KoralNative.koral_navigator_close()
    fun quit() = KoralNative.koral_navigator_quit()
}

/** kor::ProjectConfig: koral.json and the command line, as the runtime reads them. */
class ProjectConfig private constructor(private var handle: MemorySegment) : AutoCloseable {
    private val native: MemorySegment get() = handle.also { check(it != MemorySegment.NULL) { "the project config was closed" } }

    /** The scene to open first. */
    val scene: String get() = KoralNative.koral_project_scene(native)
    val hotReload: Boolean get() = KoralNative.koral_project_hot_reload(native)

    val appSettings: AppSettings
        get() = Arena.ofConfined().use { a ->
            val s = a.allocate(KoralLayouts.KoralAppSettings)
            KoralNative.koral_project_app_settings(native, s)
            val f = Fields(s, KoralLayouts.KoralAppSettings)
            fun text(field: String): String? =
                s.get(ADDRESS, KoralLayouts.KoralAppSettings.byteOffset(java.lang.foreign.MemoryLayout.PathElement.groupElement(field)))
                    .takeIf { it != MemorySegment.NULL }?.let(Native::kString)?.takeIf { it.isNotEmpty() }
            AppSettings(API.of(f.readInt("api")), WindowPlatform.of(f.readInt("platform")), f.readInt("frames_in_flight"),
                        text("gpu"))
        }

    val windowSettings: WindowSettings
        get() = Arena.ofConfined().use { a ->
            val s = a.allocate(KoralLayouts.KoralWindowSettings)
            KoralNative.koral_project_window_settings(native, s)
            WindowSettings.from(s)
        }

    override fun close() {
        if (handle == MemorySegment.NULL) return
        KoralNative.koral_project_destroy(handle)
        handle = MemorySegment.NULL
    }

    companion object {
        /** Reads koral.json (looked for from [searchFrom] up) and [args]. */
        fun load(args: List<String>, searchFrom: String = System.getProperty("user.dir")): ProjectConfig = Arena.ofConfined().use { a ->
            val argv = a.allocate(ADDRESS, maxOf(1, args.size).toLong())
            args.forEachIndexed { i, s -> argv.setAtIndex(ADDRESS, i.toLong(), a.allocateFrom(s)) }
            ProjectConfig(checked(KoralNative.koral_project_load(searchFrom, args.size, argv), "reading the project"))
        }

        /** What the runtime's command line takes. */
        val usage: String get() = KoralNative.koral_project_usage()
    }
}

/**
 * kor::Scene, written in Kotlin: override the hooks it needs. Its window, input, clock, debug lines and frame
 * graph are properties; inside it they are also Window.current and the rest.
 *
 * ```
 * class Menu : Scene() {
 *     override fun initialize() { window.title = "Menu" }
 *     override fun update() { if (input.isKeyPressed(Key.eEsc)) Navigator.quit() }
 * }
 * ```
 */
abstract class Scene : Owner() {
    internal var native: MemorySegment = MemorySegment.NULL

    open fun initialize() {}
    open fun fixedUpdate() {}
    open fun update() {}
    open fun lateUpdate() {}
    /** Records into the frame, after the frame graph's passes: for a scene without one. */
    open fun render(commands: CommandBuffer) {}
    open fun onResize(width: Int, height: Int) {}
    /** Its window was minimised, or another scene was pushed over it. */
    open fun onSuspend() {}
    open fun onResume() {}
    /** Its window's close button: false keeps it open (to ask first). */
    open fun onCloseRequested(): Boolean = true
    /** Before it goes, with the GPU idle. What it owns is closed after this. */
    open fun shutdown() {}
    /** What [App.reloadScenes] keeps across the reload, as JSON; null: nothing. */
    open fun saveState(): String? = null
    open fun loadState(json: String) {}

    internal val updateHooks = mutableListOf<() -> Unit>()
    private val coroutinesMade = lazy { SceneCoroutines(this) }
    internal val coroutines by coroutinesMade
    internal fun cancelCoroutines() { if (coroutinesMade.isInitialized()) coroutines.cancel() }
    internal var stateArena: Arena? = null

    /** Runs [hook] each frame before [update]: what a library driving something per frame (an interface) adds. */
    fun beforeUpdate(hook: () -> Unit) { updateHooks += hook }

    private val open: MemorySegment get() = native.also { if (it == MemorySegment.NULL) throw KoralException("$this is not open") }

    val isOpen: Boolean get() = native != MemorySegment.NULL && KoralNative.koral_app_is_open(native)
    val name: String get() = if (native == MemorySegment.NULL) "" else KoralNative.koral_scene_name(native)
    val graph: FrameGraph get() = FrameGraph(KoralNative.koral_scene_graph(open))
    val window: Window get() = Window(KoralNative.koral_scene_scene_window(open))
    val input: Input get() = Input(KoralNative.koral_scene_scene_input(open))
    val time: Time get() = Time(KoralNative.koral_scene_scene_time(open))
    val debug: DebugDraw get() = DebugDraw(KoralNative.koral_scene_scene_debug(open))

    /** AddView: a frame graph of its own, drawing into an image ([target]'s size and format). */
    fun addView(name: String, target: OffscreenSettings = OffscreenSettings(title = name)): View =
        Arena.ofConfined().use { a -> View(checked(KoralNative.koral_scene_add_view(open, name, target.native(a)), "adding the view $name")) }
    fun removeView(name: String) = KoralNative.koral_scene_remove_view(open, name)
    fun findView(name: String): View? = KoralNative.koral_scene_find_view(open, name).takeIf { it != MemorySegment.NULL }?.let(::View)
    val views: List<View> get() = List(KoralNative.koral_scene_view_count(open)) { View(KoralNative.koral_scene_view(open, it)) }

    override fun toString() = "${javaClass.simpleName} '${if (native != MemorySegment.NULL) name else "unopened"}'"

    companion object {
        /** The scene whose code is running on this thread, or null. */
        val current: Scene? get() = SceneBridge.live[KoralNative.koral_scene_current().address()]
    }
}

/** Kotlin scenes as kor::Scene callbacks: one upcall stub per hook, shared by every scene. */
internal object SceneBridge {
    val live = ConcurrentHashMap<Long, Scene>()

    fun of(native: MemorySegment): Scene = live[native.address()]
        ?: throw KoralException("the scene opened is not a Kotlin scene of this application")

    private val lookup = MethodHandles.lookup()
    private fun stub(name: String, descriptor: FunctionDescriptor): MemorySegment =
        Native.linker.upcallStub(lookup.findStatic(SceneBridge::class.java, name, descriptor.toMethodType()), descriptor, Arena.global())

    private val hook = FunctionDescriptor.ofVoid(ADDRESS, ADDRESS)
    private val initializeStub by lazy { stub("onInitialize", hook) }
    private val fixedUpdateStub by lazy { stub("onFixedUpdate", hook) }
    private val updateStub by lazy { stub("onUpdate", hook) }
    private val lateUpdateStub by lazy { stub("onLateUpdate", hook) }
    private val renderStub by lazy { stub("onRender", FunctionDescriptor.ofVoid(ADDRESS, ADDRESS, ADDRESS)) }
    private val resizeStub by lazy { stub("onResize", FunctionDescriptor.ofVoid(ADDRESS, JAVA_INT, JAVA_INT, ADDRESS)) }
    private val suspendStub by lazy { stub("onSuspend", hook) }
    private val resumeStub by lazy { stub("onResume", hook) }
    private val closeRequestedStub by lazy { stub("onCloseRequested", FunctionDescriptor.of(JAVA_BOOLEAN, ADDRESS, ADDRESS)) }
    private val shutdownStub by lazy { stub("onShutdown", hook) }
    private val saveStateStub by lazy { stub("onSaveState", FunctionDescriptor.of(ADDRESS, ADDRESS)) }
    private val loadStateStub by lazy { stub("onLoadState", FunctionDescriptor.ofVoid(ADDRESS, ADDRESS)) }
    private val destroyStub by lazy { stub("onDestroy", FunctionDescriptor.ofVoid(ADDRESS)) }
    val factoryStub: MemorySegment by lazy { stub("make", FunctionDescriptor.of(KoralLayouts.KoralSceneCallbacks, ADDRESS, ADDRESS)) }

    /** The callbacks for [scene], in [segment] (a KoralSceneCallbacks). */
    fun fill(scene: Scene, segment: MemorySegment) {
        Fields(segment, KoralLayouts.KoralSceneCallbacks)
            .address("user", Handles.put(scene))
            .address("initialize", initializeStub)
            .address("fixed_update", fixedUpdateStub)
            .address("update", updateStub)
            .address("late_update", lateUpdateStub)
            .address("on_resize", resizeStub)
            .address("on_suspend", suspendStub)
            .address("on_resume", resumeStub)
            .address("on_close_requested", closeRequestedStub)
            .address("shutdown", shutdownStub)
            .address("save_state", saveStateStub)
            .address("load_state", loadStateStub)
            .address("destroy", destroyStub)
        // Only a scene that renders gets the hook: the runtime records one for each that has it.
        val renders = scene.javaClass.getMethod("render", CommandBuffer::class.java).declaringClass != Scene::class.java
        if (renders) Fields(segment, KoralLayouts.KoralSceneCallbacks).address("render", renderStub)
    }

    private inline fun run(native: MemorySegment, user: MemorySegment, what: String, body: (Scene) -> Unit) {
        val scene = Handles.get<Scene>(user) ?: return
        if (scene.native != native) {
            scene.native = native
            live[native.address()] = scene
        }
        try { body(scene) } catch (e: Throwable) { failed(scene, what, e) }
    }

    private fun failed(scene: Scene, what: String, e: Throwable) {
        Log.error("[${scene.javaClass.simpleName}] $what threw $e\n${e.stackTraceToString()}")
        for (listener in App.hookFailed) runCatching { listener(scene, what, e) }
    }

    @JvmStatic fun onInitialize(native: MemorySegment, user: MemorySegment) = run(native, user, "initialize") { it.initialize() }
    @JvmStatic fun onFixedUpdate(native: MemorySegment, user: MemorySegment) = run(native, user, "fixedUpdate") { it.fixedUpdate() }
    @JvmStatic fun onUpdate(native: MemorySegment, user: MemorySegment) = run(native, user, "update") { scene ->
        for (hook in scene.updateHooks.toList()) {
            try { hook() } catch (e: Throwable) { failed(scene, "a frame hook", e) }
        }
        scene.update()
    }
    @JvmStatic fun onLateUpdate(native: MemorySegment, user: MemorySegment) = run(native, user, "lateUpdate") { it.lateUpdate() }
    @JvmStatic fun onRender(native: MemorySegment, commands: MemorySegment, user: MemorySegment) =
        run(native, user, "render") { it.render(CommandBuffer(commands)) }
    @JvmStatic fun onResize(native: MemorySegment, width: Int, height: Int, user: MemorySegment) =
        run(native, user, "onResize") { it.onResize(width, height) }
    @JvmStatic fun onSuspend(native: MemorySegment, user: MemorySegment) = run(native, user, "onSuspend") { it.onSuspend() }
    @JvmStatic fun onResume(native: MemorySegment, user: MemorySegment) = run(native, user, "onResume") { it.onResume() }
    @JvmStatic fun onCloseRequested(native: MemorySegment, user: MemorySegment): Boolean {
        var close = true
        run(native, user, "onCloseRequested") { close = it.onCloseRequested() }
        return close
    }
    @JvmStatic fun onShutdown(native: MemorySegment, user: MemorySegment) = run(native, user, "shutdown") {
        try { it.cancelCoroutines(); it.shutdown() } finally { it.closeOwned() }
    }

    /** The state, as a C string kept until the next save (or the scene goes). */
    @JvmStatic fun onSaveState(user: MemorySegment): MemorySegment {
        val scene = Handles.get<Scene>(user) ?: return MemorySegment.NULL
        val json = try { scene.saveState() } catch (e: Throwable) { failed(scene, "saveState", e); null } ?: return MemorySegment.NULL
        scene.stateArena?.close()
        return Arena.ofShared().also { scene.stateArena = it }.allocateFrom(json)
    }
    @JvmStatic fun onLoadState(json: MemorySegment, user: MemorySegment) {
        val scene = Handles.get<Scene>(user) ?: return
        try { scene.loadState(Native.kString(json)) } catch (e: Throwable) { failed(scene, "loadState", e) }
    }

    @JvmStatic fun onDestroy(user: MemorySegment) {
        val scene = Handles.get<Scene>(user)
        Handles.free(user)
        if (scene != null) {
            live.remove(scene.native.address(), scene)
            scene.closeOwned()   // a scene that never got to shut down
            scene.stateArena?.close()
            scene.stateArena = null
            scene.native = MemorySegment.NULL
        }
    }

    /** A registered scene's factory: the Kotlin scene, made, as callbacks — every member zero when it could not be. */
    @JvmStatic fun make(argumentsJson: MemorySegment, user: MemorySegment): MemorySegment {
        val segment = Arena.ofAuto().allocate(KoralLayouts.KoralSceneCallbacks)
        val factory = Handles.get<(SceneArgs) -> Scene>(user) ?: return segment
        val made = mutableListOf<AutoCloseable>()
        Ownership.constructing.set(made)
        try {
            val scene = factory(SceneArgs.fromJson(Native.kString(argumentsJson)))
            for (closeable in made) scene.own(closeable)
            fill(scene, segment)
        } catch (e: Throwable) {
            for (closeable in made.asReversed()) runCatching { closeable.close() }
            Log.error("[koral] a scene could not be made: $e\n${e.stackTraceToString()}")
        } finally {
            Ownership.constructing.set(null)
        }
        return segment
    }
}
