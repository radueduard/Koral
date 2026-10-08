package koral.net

import java.lang.foreign.Arena
import java.lang.foreign.MemoryLayout.PathElement.groupElement
import java.lang.foreign.MemorySegment
import java.lang.foreign.SegmentAllocator
import java.lang.foreign.StructLayout
import java.lang.foreign.ValueLayout.ADDRESS
import java.lang.foreign.ValueLayout.JAVA_BOOLEAN
import java.lang.foreign.ValueLayout.JAVA_BYTE
import java.lang.foreign.ValueLayout.JAVA_FLOAT
import java.lang.foreign.ValueLayout.JAVA_INT
import java.lang.foreign.ValueLayout.JAVA_LONG
import java.lang.foreign.ValueLayout.JAVA_SHORT
import kotlin.time.Duration
import koral.ErrorCode
import koral.Token
import koral.interop.Native
import koral.net.interop.KnetLayouts
import koral.net.interop.KnetNative

/**
 * A network operation that failed: knet's kor::Error — [code] is eNetwork, eTimedOut, eConnectionClosed or
 * eProtocol (or eInvalidArgument). Where C++ returns a kor::Result, Kotlin throws this.
 */
class NetException(val code: ErrorCode, message: String) : RuntimeException(message)

/** knet::Endpoint: a numeric address and a port. */
data class Endpoint(val address: String, val port: Int) {
    val isV6: Boolean get() = address.contains(':')
    override fun toString() = if (isV6) "[$address]:$port" else "$address:$port"
}

/** knet::TlsOptions: how a TLS client checks whom it talks to. */
data class TlsOptions(val verifyPeer: Boolean = true, val serverName: String? = null, val caPem: String? = null)

/** UTF-8 bytes of a string, and back. */
fun bytesOf(text: String): ByteArray = text.toByteArray(Charsets.UTF_8)
fun textOf(bytes: ByteArray): String = String(bytes, Charsets.UTF_8)

/** knet::Resolve: every address [host] has, for [port]. */
suspend fun resolve(host: String, port: Int): List<Endpoint> {
    val token = Arena.ofConfined().use { a -> a.allocate(ADDRESS).let { t -> KnetNative.knet_resolve(host, port.toShort(), t) to t.get(ADDRESS, 0) } }
    return Ops.run(token.first, token.second) { op ->
        List(KnetNative.knet_op_count(op).toInt()) { i -> Ops.address(op, i) }
    }
}

/** Awaiting an operation's token, then reading what it made; its handle freed either way. */
internal object Ops {
    suspend fun <T> run(op: MemorySegment, token: MemorySegment, read: (MemorySegment) -> T): T {
        try {
            Token.adopt(token).await()
            if (KnetNative.knet_op_failed(op))
                throw NetException(ErrorCode.of(KnetNative.knet_op_error_code(op)), (KnetNative.knet_op_error(op)))
            return read(op)
        } finally {
            KnetNative.knet_op_destroy(op)
        }
    }

    /** Starts an operation: [start] gets where to put the token. */
    suspend fun <T> start(start: (Arena, MemorySegment) -> MemorySegment, read: (MemorySegment) -> T): T {
        val (op, token) = Arena.ofConfined().use { a ->
            val slot = a.allocate(ADDRESS)
            val op = start(a, slot)
            op to slot.get(ADDRESS, 0)
        }
        return run(op, token, read)
    }

    fun bytes(op: MemorySegment): ByteArray = Arena.ofConfined().use { a ->
        val size = a.allocate(JAVA_LONG)
        val data = KnetNative.knet_op_bytes(op, size)
        val n = size.get(JAVA_LONG, 0)
        if (n == 0L) ByteArray(0) else data.reinterpret(n).toArray(JAVA_BYTE)
    }

    fun address(op: MemorySegment, index: Int): Endpoint = Arena.ofConfined().use { a ->
        val port = a.allocate(JAVA_SHORT)
        val text = (KnetNative.knet_op_address(op, index.toLong(), port))
        Endpoint(text, port.get(JAVA_SHORT, 0).toInt() and 0xffff)
    }

    fun ms(d: Duration?): Int = d?.inWholeMilliseconds?.coerceIn(0, Int.MAX_VALUE.toLong())?.toInt() ?: 0

    fun tls(a: Arena, o: TlsOptions?): MemorySegment = Struct(a, KnetLayouts.KnetTlsOptions)
        .bool("verify_peer", o?.verifyPeer ?: true).string("server_name", o?.serverName).string("ca_pem", o?.caPem).segment

    fun strings(a: Arena, items: List<String>): MemorySegment =
        if (items.isEmpty()) MemorySegment.NULL else a.allocate(ADDRESS, items.size.toLong()).also { s ->
            items.forEachIndexed { i, t -> s.setAtIndex(ADDRESS, i.toLong(), a.allocateFrom(t)) }
        }

    fun data(a: Arena, bytes: ByteArray): MemorySegment = if (bytes.isEmpty()) MemorySegment.NULL else a.allocateFrom(JAVA_BYTE, *bytes)
}

/** Filling a C struct by field name, with C's layout. */
internal class Struct(private val arena: SegmentAllocator, private val layout: StructLayout, val segment: MemorySegment = arena.allocate(layout)) {
    private fun at(name: String) = layout.byteOffset(groupElement(name))
    fun bool(name: String, v: Boolean) = apply { segment.set(JAVA_BOOLEAN, at(name), v) }
    fun short(name: String, v: Int) = apply { segment.set(JAVA_SHORT, at(name), v.toShort()) }
    fun int(name: String, v: Int) = apply { segment.set(JAVA_INT, at(name), v) }
    fun long(name: String, v: Long) = apply { segment.set(JAVA_LONG, at(name), v) }
    fun float(name: String, v: Float) = apply { segment.set(JAVA_FLOAT, at(name), v) }
    fun address(name: String, v: MemorySegment) = apply { segment.set(ADDRESS, at(name), v) }
    fun string(name: String, v: String?) = address(name, if (v == null) MemorySegment.NULL else (arena as Arena).allocateFrom(v))
    fun struct(name: String, v: MemorySegment) = apply { MemorySegment.copy(v, 0, segment, at(name), v.byteSize()) }
}
