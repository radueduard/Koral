package koral

import java.lang.foreign.MemorySegment
import kotlin.coroutines.CoroutineContext
import kotlin.coroutines.resume
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.CoroutineExceptionHandler
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.CoroutineStart
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.launch
import kotlinx.coroutines.suspendCancellableCoroutine
import koral.interop.KoralNative

/**
 * kor::Token: something that will be done — GPU work submitted, a readback finished — waited for with
 * [wait], or awaited, as C++ co_awaits it:
 *
 * ```
 * override fun initialize() {
 *     launch {
 *         CommandBuffer.singleTimeCommand { it.generateMipmaps(texture) }.await()
 *         ready = true   // on the frame's thread, with this scene current again
 *     }
 * }
 * ```
 */
class Token internal constructor(internal val native: MemorySegment) {
    init {
        val handle = native
        cleaner.register(this) { KoralNative.koral_token_destroy(handle) }
    }

    val isReady: Boolean get() = KoralNative.koral_token_ready(native)
    val value: Long get() = KoralNative.koral_token_value(native)
    /** Token::Wait: blocks this thread until it is done (wait() is the JVM's own). */
    fun waitBlocking() = KoralNative.koral_token_wait(native)
    /** Marks one made with [create] done. */
    fun signal() = KoralNative.koral_token_signal(native)

    /** Suspends until it is done; resumes on the frame's thread at the start of a frame (or, with no application, on a worker). */
    suspend fun await() {
        if (isReady) return
        suspendCancellableCoroutine { continuation ->
            MainThread.whenReady(this) { if (continuation.isActive) continuation.resume(Unit) }
        }
    }

    companion object {
        /** Token::Create: one of one's own, signalled with [signal]. */
        fun create(): Token = Token(checked(KoralNative.koral_token_create(), "making a token"))
    }
}

/**
 * What resumes on the frame's thread, at the start of each frame: continuations waiting on tokens, and
 * coroutines a scene launched — each run with the scene it belongs to current.
 */
internal object MainThread {
    private class Waiting(val token: Token?, val scene: Scene?, val run: () -> Unit)
    private val waiting = ArrayList<Waiting>()

    fun whenReady(token: Token, run: () -> Unit) {
        if (App.current == null) {
            // No frames to resume in: wait on a worker, as a program with no application must.
            Thread.ofVirtual().start { token.waitBlocking(); run() }
            return
        }
        synchronized(waiting) { waiting += Waiting(token, Scene.current, run) }
    }

    fun post(scene: Scene?, run: () -> Unit) {
        synchronized(waiting) { waiting += Waiting(null, scene, run) }
    }

    /** Runs whatever is ready: called by App.frame. */
    fun resume() {
        val ready = synchronized(waiting) {
            if (waiting.isEmpty()) return
            waiting.filter { it.token == null || it.token.isReady }.also { waiting.removeAll(it.toSet()) }
        }
        for (w in ready) {
            val scope = if (w.scene != null && w.scene.isOpen) KoralNative.koral_scene_scope_enter(w.scene.native) else MemorySegment.NULL
            try {
                w.run()
            } catch (e: Throwable) {
                Log.error("[${w.scene?.javaClass?.simpleName ?: "koral"}] resumed work threw $e\n${e.stackTraceToString()}")
            } finally {
                if (scope != MemorySegment.NULL) KoralNative.koral_scene_scope_exit(scope)
            }
        }
    }
}

/** Runs a scene's coroutines on the frame's thread, at the start of a frame, with the scene current. */
internal class FrameDispatcher(private val scene: Scene?) : CoroutineDispatcher() {
    override fun dispatch(context: CoroutineContext, block: Runnable) = MainThread.post(scene) { block.run() }
}

/** A scene's coroutines: resumed between frames with the scene current, cancelled when it shuts down. */
internal class SceneCoroutines(scene: Scene) {
    private val job: Job = SupervisorJob()
    val scope = CoroutineScope(job + FrameDispatcher(scene) + CoroutineExceptionHandler { _, e ->
        Log.error("[${scene.javaClass.simpleName}] a coroutine threw $e\n${e.stackTraceToString()}")
    })
    fun cancel() = scope.cancel()
}

/**
 * Starts [block] as a coroutine of this scene: it runs now, until it first suspends, then resumes between
 * frames with the scene current — `token.await()`, `delay`, `buffer.readAsync()`. Cancelled when the scene
 * shuts down.
 */
fun Scene.launch(block: suspend CoroutineScope.() -> Unit): Job = coroutines.scope.launch(start = CoroutineStart.UNDISPATCHED, block = block)
