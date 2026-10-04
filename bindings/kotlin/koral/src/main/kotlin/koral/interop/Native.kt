package koral.interop

import java.lang.foreign.Arena
import java.lang.foreign.FunctionDescriptor
import java.lang.foreign.Linker
import java.lang.foreign.MemorySegment
import java.lang.foreign.SymbolLookup
import java.lang.foreign.ValueLayout
import java.nio.file.Files
import java.nio.file.Path

/**
 * Finds Koral's native libraries, and what every call through them shares.
 *
 * Koral's library, in order: `KORAL_LIBRARY` (the file itself); `KORAL_SDK` (an SDK, its `lib`); beside
 * the running code; then wherever the OS looks. A module — koral-ui — is looked for in `modules/` beside
 * Koral's, where an SDK and a build tree both put it.
 */
object Native {
    val linker: Linker = Linker.nativeLinker()

    private val os = System.getProperty("os.name").lowercase()
    private fun fileName(stem: String) = when {
        os.contains("win") -> "$stem.dll"
        os.contains("mac") -> "lib$stem.dylib"
        else -> "lib$stem.so"
    }

    /** Where Koral's library was loaded from. */
    lateinit var loadedFrom: Path
        private set

    val koral: SymbolLookup by lazy {
        val candidates = buildList {
            System.getenv("KORAL_LIBRARY")?.takeIf { it.isNotEmpty() }?.let { add(Path.of(it)) }
            System.getenv("KORAL_SDK")?.takeIf { it.isNotEmpty() }?.let { sdk ->
                if (os.contains("win")) add(Path.of(sdk, "bin", fileName("Koral")))
                add(Path.of(sdk, "lib", fileName("Koral")))
                add(Path.of(sdk, "lib64", fileName("Koral")))
            }
            add(Path.of(fileName("Koral")).toAbsolutePath())
        }
        val found = candidates.firstOrNull { Files.exists(it) }
            ?: throw UnsatisfiedLinkError("Koral's native library (${fileName("Koral")}) was not found: set KORAL_SDK to a Koral SDK, or KORAL_LIBRARY to the library itself. Tried $candidates")
        loadedFrom = found.toAbsolutePath()
        load(loadedFrom)
    }

    /**
     * Loads a library whose dependencies are beside it.
     *
     * Elsewhere that is the library's own business (its rpath). On Windows it is the caller's: the JVM
     * loads with the default search, which looks beside java.exe and on the PATH, never beside the
     * library — so Koral.dll, in an SDK's bin with the DLLs it links, would not open. Loading it first
     * with the altered search path is what looks there; the lookup after it finds it already loaded.
     */
    private fun load(library: Path): SymbolLookup {
        if (os.contains("win")) {
            val loadLibraryEx = linker.downcallHandle(
                SymbolLookup.libraryLookup("kernel32", Arena.global()).find("LoadLibraryExW").orElseThrow(),
                FunctionDescriptor.of(ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.JAVA_INT))
            val loaded = Arena.ofConfined().use { arena ->
                // Backslashes, which Path gives: the altered search does not take a path with forward ones.
                val path = arena.allocateFrom(library.toString(), Charsets.UTF_16LE)
                loadLibraryEx.invokeExact(path, MemorySegment.NULL, LOAD_WITH_ALTERED_SEARCH_PATH) as MemorySegment
            }
            if (loaded == MemorySegment.NULL)
                throw UnsatisfiedLinkError("$library could not be loaded: it, or a library it depends on (looked for beside it), is missing or is not for this machine")
        }
        return SymbolLookup.libraryLookup(library, Arena.global())
    }

    private const val LOAD_WITH_ALTERED_SEARCH_PATH = 0x00000008

    /** A module's library, `modules/<name>` beside Koral's — or the file `KORAL_<NAME>_LIBRARY` names. */
    fun module(stem: String, variable: String): SymbolLookup {
        koral   // Koral first: the module links against it, and must find the copy already loaded
        val explicit = System.getenv(variable)?.takeIf { it.isNotEmpty() }?.let { Path.of(it) }
        val dir = loadedFrom.parent
        val found = listOfNotNull(explicit, dir.resolve("modules").resolve(fileName(stem)), dir.resolve(fileName(stem)))
            .firstOrNull { Files.exists(it) }
            ?: throw UnsatisfiedLinkError("the module ${fileName(stem)} was not found beside Koral's library ($dir/modules); set $variable to it")
        return load(found.toAbsolutePath())
    }

    val koralUi: SymbolLookup by lazy { module("koral-ui", "KORAL_UI_LIBRARY") }

    /** A C string for @p text in @p arena; null for null. */
    fun cString(arena: Arena, text: String?): MemorySegment = if (text == null) MemorySegment.NULL else arena.allocateFrom(text)

    /** A string Koral returned (valid until the next call on this thread), copied at once; "" for null. */
    fun kString(text: MemorySegment): String =
        if (text == MemorySegment.NULL) "" else text.reinterpret(Long.MAX_VALUE).getString(0)
}
