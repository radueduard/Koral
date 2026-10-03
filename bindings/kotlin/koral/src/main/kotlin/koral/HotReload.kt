package koral

import java.lang.instrument.ClassDefinition
import java.lang.instrument.Instrumentation
import java.lang.management.ManagementFactory
import java.nio.file.FileSystems
import java.nio.file.Files
import java.nio.file.Path
import java.nio.file.StandardWatchEventKinds.ENTRY_CREATE
import java.nio.file.StandardWatchEventKinds.ENTRY_DELETE
import java.nio.file.StandardWatchEventKinds.ENTRY_MODIFY
import java.nio.file.WatchService
import java.util.concurrent.CopyOnWriteArrayList
import java.util.concurrent.atomic.AtomicReference
import java.util.zip.CRC32
import kotlin.io.path.extension
import kotlin.io.path.isDirectory
import kotlin.io.path.readBytes
import kotlin.io.path.relativeTo

/**
 * Edits applied to the running program, as C#'s are: a changed class is redefined in place, so every object
 * keeps its state and runs the new code from the next frame. Compose interfaces recompose keeping what
 * they `remember`.
 *
 * It needs Koral's jar as the JVM's agent (`-javaagent:koral.jar`), and is turned on with the system
 * property `koral.hotReload` (or `KORAL_HOT_RELOAD=1`). Then it watches:
 *  - `koral.hotReload.sources`: source directories. A change runs `koral.hotReload.compile` (the build's
 *    compile command), after which the changed classes are applied;
 *  - or, without them, the class directories on the class path, for an IDE or a `--continuous` build to
 *    write into.
 *
 * A standard JVM can only change what methods do. An edit that adds or removes a method, a field or a lambda
 * — adding a composable call usually does — needs the JetBrains Runtime run with
 * `-XX:+AllowEnhancedClassRedefinition`; elsewhere it is reported, and needs a restart.
 */
object HotReload {
    /** One applied edit: the classes it redefined, with their class files before and after. */
    class Reload(val classes: List<Class<*>>, val before: Map<String, ByteArray?>, val after: Map<String, ByteArray>)

    @Volatile private var instrumentation: Instrumentation? = null
    private val listeners = CopyOnWriteArrayList<(Reload) -> Unit>()
    /** The class files last applied, or found when watching began: what an edit is compared with. */
    private val known = java.util.concurrent.ConcurrentHashMap<String, ByteArray>()
    private val pending = AtomicReference<Map<String, ByteArray>?>(null)
    private var watcher: Thread? = null

    @JvmStatic fun premain(@Suppress("UNUSED_PARAMETER") args: String?, inst: Instrumentation) { instrumentation = inst }
    @JvmStatic fun agentmain(@Suppress("UNUSED_PARAMETER") args: String?, inst: Instrumentation) { instrumentation = inst }

    /** Whether classes can be redefined: Koral's jar is the JVM's agent. */
    val isAvailable: Boolean get() = instrumentation?.isRedefineClassesSupported == true

    /** Whether edits may change a class's shape — add methods, fields, lambdas — and not only method bodies. */
    val isStructural: Boolean by lazy {
        ManagementFactory.getRuntimeMXBean().inputArguments.any { it == "-XX:+AllowEnhancedClassRedefinition" }
    }

    /** Whether it was asked for, by `koral.hotReload` or `KORAL_HOT_RELOAD`. */
    val isRequested: Boolean
        get() = System.getProperty("koral.hotReload")?.let { it != "false" } ?: (System.getenv("KORAL_HOT_RELOAD")?.let { it == "1" || it == "true" } ?: false)

    /** Called on the frame's thread after every applied edit. */
    fun onReloaded(listener: (Reload) -> Unit) { listeners += listener }
    fun removeListener(listener: (Reload) -> Unit) { listeners -= listener }

    // ---- applying ---------------------------------------------------------------------------------------

    /**
     * Redefines the loaded classes among [classes] (binary name → class file) in place, and tells the
     * listeners. Classes not loaded yet need nothing: they are read from their new files when first used.
     * Returns the classes it changed; throws [KoralException] when the JVM refuses the edit.
     */
    fun apply(classes: Map<String, ByteArray>): List<Class<*>> {
        val inst = instrumentation ?: throw KoralException("hot reload needs Koral's jar as the JVM's agent: -javaagent:<koral.jar>")
        // The program's own classes: not the copies made below.
        val loaded = inst.allLoadedClasses.filter {
            it.name in classes && inst.isModifiableClass(it) && it.classLoader != null && it.classLoader !is CopyLoader
        }
        if (loaded.isEmpty()) return emptyList()
        try {
            inst.redefineClasses(*loaded.map { ClassDefinition(it, classes.getValue(it.name)) }.toTypedArray())
        } catch (e: UnsupportedOperationException) {
            throw KoralException(
                "this edit changes the shape of ${loaded.joinToString { it.simpleName }} (adds or removes a method, field or lambda), " +
                "which " + (if (isStructural) "the JVM refused: ${e.message}" else
                "needs the JetBrains Runtime with -XX:+AllowEnhancedClassRedefinition") + ". Restart to apply it.")
        } catch (e: ClassFormatError) {
            throw KoralException("the edited classes could not be read: ${e.message}")
        }
        if (isStructural) for (c in loaded) initializeNewStatics(c, classes.getValue(c.name))
        val before = loaded.associate { it.name to (known[it.name] ?: classFileOf(it)) }
        val after = loaded.associate { it.name to classes.getValue(it.name) }
        known.putAll(after)
        val reload = Reload(loaded, before, after)
        for (listener in listeners) {
            try { listener(reload) } catch (e: Throwable) { Log.error("[koral] a hot-reload listener threw $e\n${e.stackTraceToString()}") }
        }
        return loaded
    }

    /** The class file a class was loaded from — before any edit, as long as its file has not changed since. */
    private fun classFileOf(c: Class<*>): ByteArray? =
        c.classLoader?.getResourceAsStream(c.name.replace('.', '/') + ".class")?.use { it.readBytes() }

    /**
     * A redefined class's static initializer does not run again, so a static field the edit added is
     * null — Compose keeps its lambdas in such fields (`ComposableSingletons`). Their values are taken from a
     * copy of the new class, initialized on its own.
     */
    private fun initializeNewStatics(c: Class<*>, bytes: ByteArray) {
        val fields = c.declaredFields.filter {
            java.lang.reflect.Modifier.isStatic(it.modifiers) && !it.type.isPrimitive && !java.lang.reflect.Modifier.isFinal(it.modifiers)
        }.filter { it.trySetAccessible() && it.get(null) == null }
        if (fields.isEmpty()) return
        val copy = CopyLoader(c.classLoader).define(c.name, bytes)
        for (field in fields) {
            try {
                val twin = copy.getDeclaredField(field.name).also { it.isAccessible = true }
                field.set(null, twin.get(null))
            } catch (e: Throwable) {
                Log.warn("[koral] hot reload could not initialize ${c.simpleName}.${field.name}: $e")
            }
        }
    }

    /** Applies what the watcher found, if anything: on the frame's thread, between frames. */
    internal fun poll() {
        val classes = pending.getAndSet(null) ?: return
        try {
            val changed = apply(classes)
            if (changed.isNotEmpty()) Log.info("[koral] hot reload: applied ${changed.joinToString { it.simpleName }}")
        } catch (e: KoralException) {
            Log.error("[koral] hot reload: ${e.message}")
        }
    }

    // ---- watching ---------------------------------------------------------------------------------------

    /** Starts watching, if asked for and possible. Called when the application starts. */
    internal fun startIfRequested() {
        if (!isRequested || watcher != null) return
        if (!isAvailable) {
            Log.warn("[koral] hot reload was asked for, but Koral's jar is not the JVM's agent (-javaagent:<koral.jar>)")
            return
        }
        val classDirs = System.getProperty("java.class.path").split(java.io.File.pathSeparator)
            .map { Path.of(it) }.filter { it.isDirectory() }
        val sources = System.getProperty("koral.hotReload.sources")?.split(java.io.File.pathSeparator)
            ?.filter { it.isNotBlank() }?.map { Path.of(it) }?.filter { it.isDirectory() }.orEmpty()
        val compile = System.getProperty("koral.hotReload.compile")?.takeIf { it.isNotBlank() }
        val snapshot = ClassSnapshot(classDirs)
        known.putAll(snapshot.files())
        val watched = if (sources.isNotEmpty() && compile != null) sources else classDirs
        watcher = Thread({ watch(watched, compile.takeIf { sources.isNotEmpty() }, snapshot) }, "koral-hot-reload").apply {
            isDaemon = true
            start()
        }
        Log.info("[koral] hot reload: watching ${watched.joinToString()}" +
                 (if (isStructural) "" else " (method bodies only: run on the JetBrains Runtime with -XX:+AllowEnhancedClassRedefinition for any edit)"))
    }

    private fun watch(roots: List<Path>, compile: String?, snapshot: ClassSnapshot) {
        val service = FileSystems.getDefault().newWatchService()
        fun register(dir: Path) = Files.walk(dir).use { s -> s.filter { it.isDirectory() }.forEach { it.register(service, ENTRY_CREATE, ENTRY_MODIFY, ENTRY_DELETE) } }
        roots.forEach(::register)
        while (true) {
            val key = service.take()
            key.pollEvents().forEach { event ->
                val dir = key.watchable() as Path
                val path = dir.resolve(event.context() as Path)
                if (event.kind() == ENTRY_CREATE && path.isDirectory()) runCatching { register(path) }
            }
            key.reset()
            settle(service)          // an editor's save, or a compiler's output, is many events
            if (compile != null && !compile(compile)) continue
            val changed = snapshot.changes()
            if (changed.isNotEmpty()) pending.getAndUpdate { (it ?: emptyMap()) + changed }
        }
    }

    private fun settle(service: WatchService) {
        while (true) {
            val more = service.poll(150, java.util.concurrent.TimeUnit.MILLISECONDS) ?: return
            more.pollEvents(); more.reset()
        }
    }

    private fun compile(command: String): Boolean {
        Log.info("[koral] hot reload: compiling")
        val process = ProcessBuilder(words(command)).redirectErrorStream(true).start()
        val output = process.inputStream.bufferedReader().readText()
        val ok = process.waitFor() == 0
        if (!ok) Log.error("[koral] hot reload: the build failed\n$output")
        return ok
    }

    private class CopyLoader(parent: ClassLoader) : ClassLoader(parent) {
        fun define(name: String, bytes: ByteArray): Class<*> = defineClass(name, bytes, 0, bytes.size)
    }

    /** A command line's words: split at spaces, except inside double quotes (a path with spaces in it). */
    internal fun words(command: String): List<String> {
        val words = mutableListOf<String>()
        val word = StringBuilder()
        var quoted = false
        var any = false
        for (c in command) when {
            c == '"' -> { quoted = !quoted; any = true }
            c == ' ' && !quoted -> { if (any) words += word.toString(); word.clear(); any = false }
            else -> { word.append(c); any = true }
        }
        if (any) words += word.toString()
        return words
    }

    /** The class files under some directories, and which of them changed since last asked. */
    internal class ClassSnapshot(private val dirs: List<Path>) {
        private var sums = scan().mapValues { it.value.first }

        fun files(): Map<String, ByteArray> = scan().mapValues { it.value.second.readBytes() }

        private fun scan(): Map<String, Pair<Long, Path>> = buildMap {
            for (dir in dirs) {
                if (!Files.isDirectory(dir)) continue
                Files.walk(dir).use { files ->
                    files.filter { it.extension == "class" && !it.fileName.toString().startsWith("module-info") }.forEach { file ->
                        val name = file.relativeTo(dir).toString().removeSuffix(".class").replace(java.io.File.separatorChar, '.')
                        val crc = CRC32().apply { update(file.readBytes()) }.value
                        put(name, crc to file)
                    }
                }
            }
        }

        fun changes(): Map<String, ByteArray> {
            val now = scan()
            val changed = now.filter { (name, v) -> sums[name] != null && sums[name] != v.first }
            // A class new since the last look is what a later edit of it is compared with.
            now.filter { it.key !in sums }.forEach { (name, v) -> known.putIfAbsent(name, v.second.readBytes()) }
            sums = now.mapValues { it.value.first }
            return changed.mapValues { it.value.second.readBytes() }
        }
    }
}
