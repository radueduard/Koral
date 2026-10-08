package koral

import java.lang.foreign.MemorySegment
import java.lang.ref.Cleaner
import koral.interop.KoralNative

internal val cleaner: Cleaner = Cleaner.create()

/**
 * kor::Error: what went wrong — its code and message, and the whole chain of causes in [history] (the shader
 * that did not compile, under the pipeline that could not be built from it).
 */
class Error(val code: ErrorCode, val message: String, val history: String) {
    override fun toString() = history.ifEmpty { message }

    companion object {
        /** kor::Describe(code). */
        fun describe(code: ErrorCode): String = KoralNative.koral_error_describe(code.value)
    }
}

/**
 * What every Koral resource is: kor::Resource<T> and kor::ResourceRef<const T> in one.
 *
 * A resource a builder made is owned: it lives until it is closed — at the end of a `use { }`, or with the
 * scene that made it (made while a scene is being made, or in one of its hooks), or with the application
 * (made anywhere else). Anything else — an image a pass looks up, a framebuffer's attachment — is borrowed:
 * closing it, or the garbage collector finding it unreachable, lets go of the reference, not the resource.
 *
 * As in C++, a build that fails does not throw: it makes a [isPoisoned] resource, which says why
 * ([failure]), is logged once, and — for shaders and what is made from them — repairs itself when the file
 * is fixed. Passing a poisoned resource on to a builder or a command poisons that, naming it as the cause.
 */
/**
 * The Kotlin object for a KoralResource* that a module's own C interface returned — a buffer, an image, whatever
 * kind the handle says it is; null for a null handle. Ask for the kind it is: one of another kind fails where it is
 * first used as [T]. The handle is the object's from here on: borrowed, it is let go of when the object is closed
 * or collected.
 */
fun <T : Resource> resourceFromHandle(native: MemorySegment): T? = Resource.wrap(native)

abstract class Resource internal constructor(handle: MemorySegment) : AutoCloseable {
    private var handle: MemorySegment = handle
    /** Owned: closed by its owner, or by close(). Borrowed: let go of by the cleaner, from any thread. */
    val isOwned: Boolean = KoralNative.koral_resource_owned(handle)
    private val cleanable = if (!isOwned) cleaner.register(this, Release(handle)) else null

    init {
        if (isOwned) Ownership.adopt(this)
    }

    private class Release(val handle: MemorySegment) : Runnable {
        override fun run() = KoralNative.koral_resource_release(handle)
    }

    /** The native handle, checked: a closed resource refuses to be used. */
    internal val native: MemorySegment
        get() {
            check(handle != MemorySegment.NULL) { "${javaClass.simpleName} was closed" }
            return handle
        }

    /** Its KoralResource*: what a module's own C interface (koral-ui's kui_image) is given. Checked. */
    val nativeHandle: MemorySegment get() = native

    val isClosed: Boolean get() = handle == MemorySegment.NULL
    /** ResourceRef::Alive: the resource still exists, poisoned or not. */
    val isAlive: Boolean get() = !isClosed && KoralNative.koral_resource_alive(handle)
    /** Resource::Valid: it exists and is usable. */
    val isValid: Boolean get() = !isClosed && KoralNative.koral_resource_valid(handle)
    /** Resource::Poisoned: its build failed; [failure] says why. */
    val isPoisoned: Boolean get() = !isClosed && KoralNative.koral_resource_poisoned(handle)

    /** Resource::Failure: why it is poisoned, or null. */
    val failure: Error?
        get() = if (!isPoisoned) null else Error(ErrorCode.of(KoralNative.koral_resource_error_code(native)),
            KoralNative.koral_resource_error_message(native), KoralNative.koral_resource_error_history(native))

    /** Its name, for diagnostics; only the owner may set it. */
    var name: String
        get() = KoralNative.koral_resource_name(native)
        set(value) {
            KoralNative.koral_resource_set_name(native, value)
            checkLastError()
        }

    /** Resource::Retry: builds a poisoned resource again, now. True when that worked. */
    fun retry(): Boolean = KoralNative.koral_resource_retry(native)

    /** Frees an owned resource (with the GPU's use of it finished first, as Koral always does), or lets go of a borrowed one. */
    override fun close() {
        val released = handle
        if (released == MemorySegment.NULL) return
        handle = MemorySegment.NULL   // closed before anything else looks at it
        if (cleanable != null) cleanable.clean() else {
            Ownership.release(this)
            KoralNative.koral_resource_release(released)
        }
    }

    /** The same resource: two handles, owned or borrowed, onto one object. */
    override fun equals(other: Any?): Boolean =
        other is Resource && !isClosed && !other.isClosed &&
            KoralNative.koral_resource_identity(handle).address() == KoralNative.koral_resource_identity(other.handle).address()

    override fun hashCode(): Int = if (isClosed) 0 else KoralNative.koral_resource_identity(handle).address().hashCode()

    override fun toString(): String {
        if (isClosed) return "${javaClass.simpleName} (closed)"
        val name = name
        return javaClass.simpleName + (if (name.isNotEmpty()) " '$name'" else "") + (if (isPoisoned) " (poisoned)" else "")
    }

    internal companion object {
        /** The Kotlin object for a handle Koral returned: null for none. */
        @Suppress("UNCHECKED_CAST")
        fun <T : Resource> wrap(native: MemorySegment): T? {
            if (native == MemorySegment.NULL) return null
            val made: Resource = when (KoralNative.koral_resource_kind(native)) {
                1 -> Buffer(native)
                2 -> Image(native)
                3 -> ImageView(native)
                4 -> Sampler(native)
                5 -> BufferView(native)
                6 -> Shader(native)
                7 -> GraphicsPipeline(native)
                8 -> ComputePipeline(native)
                9 -> RayTracingPipeline(native)
                10 -> DescriptorSet(native)
                11 -> DescriptorSetLayout(native)
                12 -> Framebuffer(native)
                13 -> Mesh(native)
                14 -> AccelerationStructure(native)
                else -> throw KoralException("Koral returned a resource of a kind this binding does not know")
            }
            return made as T
        }

        /** What a builder built: owned, possibly poisoned, never null. */
        fun <T : Resource> built(native: MemorySegment, what: String): T = wrap(checked(native, what))!!

        /** A borrowed handle Koral returned, checked: null for none. */
        fun <T : Resource> borrowed(native: MemorySegment): T? = wrap<T>(native).also { checkLastError() }
    }
}

/** The native handle of a resource that may be absent. */
internal fun handleOf(resource: Resource?): MemorySegment = resource?.native ?: MemorySegment.NULL

/**
 * kor::Builder: what every builder is. Each setter is the C++ one, called on a C++ builder, so what it
 * checks and what it warns about are the same; `build()` makes the resource, poisoned if it cannot be built.
 * A builder can be built from more than once, and set again in between. It holds configuration and
 * references, never GPU objects: the garbage collector frees it.
 */
abstract class Builder internal constructor(native: MemorySegment) {
    internal val native: MemorySegment = checked(native, "making a builder")

    init {
        val handle = this.native
        cleaner.register(this) { KoralNative.koral_builder_destroy(handle) }
    }

    /** Builder::HasErrors: whether something set so far will make the build fail. */
    val hasErrors: Boolean get() = KoralNative.koral_builder_has_errors(native)
}
