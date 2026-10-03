package koral.interop

import java.lang.foreign.Arena
import java.lang.foreign.Linker
import java.lang.foreign.MemorySegment
import java.lang.foreign.SymbolLookup
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
        SymbolLookup.libraryLookup(found, Arena.global())
    }

    /** A module's library, `modules/<name>` beside Koral's — or the file `KORAL_<NAME>_LIBRARY` names. */
    fun module(stem: String, variable: String): SymbolLookup {
        koral   // Koral first: the module links against it, and must find the copy already loaded
        val explicit = System.getenv(variable)?.takeIf { it.isNotEmpty() }?.let { Path.of(it) }
        val dir = loadedFrom.parent
        val found = listOfNotNull(explicit, dir.resolve("modules").resolve(fileName(stem)), dir.resolve(fileName(stem)))
            .firstOrNull { Files.exists(it) }
            ?: throw UnsatisfiedLinkError("the module ${fileName(stem)} was not found beside Koral's library ($dir/modules); set $variable to it")
        return SymbolLookup.libraryLookup(found, Arena.global())
    }

    val koralUi: SymbolLookup by lazy { module("koral-ui", "KORAL_UI_LIBRARY") }

    /** A C string for @p text in @p arena; null for null. */
    fun cString(arena: Arena, text: String?): MemorySegment = if (text == null) MemorySegment.NULL else arena.allocateFrom(text)

    /** A string Koral returned (valid until the next call on this thread), copied at once; "" for null. */
    fun kString(text: MemorySegment): String =
        if (text == MemorySegment.NULL) "" else text.reinterpret(Long.MAX_VALUE).getString(0)
}
