package koral.net

import java.lang.foreign.Arena
import java.lang.foreign.MemorySegment
import java.lang.foreign.ValueLayout.JAVA_SHORT
import java.lang.ref.Cleaner
import kotlin.time.Duration
import kotlin.time.Duration.Companion.seconds
import koral.ErrorCode
import koral.interop.KoralNative
import koral.interop.Native
import koral.net.interop.KnetNative

internal val cleaner: Cleaner = Cleaner.create()

/** knet::ConnectOptions. */
data class ConnectOptions(val timeout: Duration? = 10.seconds, val tls: TlsOptions? = null)

/** knet::TcpStream: a connected byte stream, TCP or TLS. Every wait suspends; a failure throws [NetException]. */
class TcpStream internal constructor(native: MemorySegment) : AutoCloseable {
    internal var native: MemorySegment = native
        private set
    private class Free(val n: MemorySegment) : Runnable { @Volatile var owned = true; override fun run() { if (owned) KnetNative.knet_stream_destroy(n) } }
    private val free = Free(native)
    private val cleanup = cleaner.register(this, free)

    companion object {
        suspend fun connect(host: String, port: Int, options: ConnectOptions = ConnectOptions()): TcpStream =
            Ops.start({ a, t -> KnetNative.knet_tcp_connect(host, port.toShort(), Ops.ms(options.timeout),
                                                            if (options.tls == null) MemorySegment.NULL else Ops.tls(a, options.tls), t) }) { op ->
                TcpStream(KnetNative.knet_op_take_stream(op))
            }
    }

    /** What has arrived: at least a byte, at most [maxBytes]. */
    suspend fun readSome(maxBytes: Int = 65536, timeout: Duration? = null): ByteArray =
        Ops.start({ _, t -> KnetNative.knet_stream_read_some(native, maxBytes.toLong(), Ops.ms(timeout), t) }, Ops::bytes)
    suspend fun readExactly(bytes: Int, timeout: Duration? = null): ByteArray =
        Ops.start({ _, t -> KnetNative.knet_stream_read_exactly(native, bytes.toLong(), Ops.ms(timeout), t) }, Ops::bytes)
    /** Up to and including [delimiter]: a line, an HTTP header. */
    suspend fun readUntil(delimiter: String = "\n", limit: Int = 64 * 1024, timeout: Duration? = null): String =
        Ops.start({ _, t -> KnetNative.knet_stream_read_until(native, delimiter, limit.toLong(), Ops.ms(timeout), t) }) { textOf(Ops.bytes(it)) }
    suspend fun readToEnd(limit: Int = 64 * 1024 * 1024, timeout: Duration? = null): ByteArray =
        Ops.start({ _, t -> KnetNative.knet_stream_read_to_end(native, limit.toLong(), Ops.ms(timeout), t) }, Ops::bytes)
    suspend fun write(data: ByteArray, timeout: Duration? = null) =
        Ops.start({ a, t -> KnetNative.knet_stream_write(native, Ops.data(a, data), data.size.toLong(), Ops.ms(timeout), t) }) { }
    suspend fun write(text: String, timeout: Duration? = null) = write(bytesOf(text), timeout)

    fun shutdown() = KnetNative.knet_stream_close(native)
    val secure: Boolean get() = KnetNative.knet_stream_secure(native)
    val local: Endpoint get() = endpoint { p -> KnetNative.knet_stream_local(native, p) }
    val remote: Endpoint get() = endpoint { p -> KnetNative.knet_stream_remote(native, p) }
    private fun endpoint(get: (MemorySegment) -> String) = Arena.ofConfined().use { a ->
        val p = a.allocate(JAVA_SHORT)
        val text = get(p)
        Endpoint(text, p.get(JAVA_SHORT, 0).toInt() and 0xffff)
    }

    /** Hands the native stream over (to a WebSocket): this object no longer owns it. */
    internal fun release(): MemorySegment { free.owned = false; cleanup.clean(); return native.also { native = MemorySegment.NULL } }

    override fun close() {
        if (native == MemorySegment.NULL) return
        cleanup.clean()
        native = MemorySegment.NULL
    }
}

/** knet::TlsServerOptions. */
data class TlsServerOptions(val certificatePem: String, val privateKeyPem: String)

/** knet::TcpListener. */
class TcpListener private constructor(private val native: MemorySegment) : AutoCloseable {
    private val cleanup = cleaner.register(this) { KnetNative.knet_listener_destroy(native) }

    companion object {
        /** Listens on [port] (0: any free one, see [port]). */
        fun listen(port: Int, address: String = "0.0.0.0", tls: TlsServerOptions? = null): TcpListener {
            val native = KnetNative.knet_listener_listen(port.toShort(), address, tls?.certificatePem, tls?.privateKeyPem)
            if (native == MemorySegment.NULL) throw NetException(ErrorCode.eNetwork, KoralNative.koral_last_error())
            return TcpListener(native)
        }
    }

    /** The next connection; a TLS client whose handshake fails is skipped. */
    suspend fun accept(): TcpStream = Ops.start({ _, t -> KnetNative.knet_listener_accept(native, t) }) { TcpStream(KnetNative.knet_op_take_stream(it)) }
    val port: Int get() = KnetNative.knet_listener_port(native).toInt() and 0xffff
    fun shutdown() = KnetNative.knet_listener_close(native)
    override fun close() = cleanup.clean()
}

/** knet::Datagram. */
class Datagram(val from: Endpoint, val data: ByteArray)

/** knet::UdpSocket. */
class UdpSocket private constructor(private val native: MemorySegment) : AutoCloseable {
    private val cleanup = cleaner.register(this) { KnetNative.knet_udp_destroy(native) }

    companion object {
        fun bind(port: Int = 0, address: String = "0.0.0.0"): UdpSocket {
            val native = KnetNative.knet_udp_bind(port.toShort(), address)
            if (native == MemorySegment.NULL) throw NetException(ErrorCode.eNetwork, KoralNative.koral_last_error())
            return UdpSocket(native)
        }
    }

    suspend fun receive(timeout: Duration? = null): Datagram =
        Ops.start({ _, t -> KnetNative.knet_udp_receive(native, Ops.ms(timeout), t) }) { Datagram(Ops.address(it, 0), Ops.bytes(it)) }
    suspend fun sendTo(to: Endpoint, data: ByteArray) =
        Ops.start({ a, t -> KnetNative.knet_udp_send_to(native, to.address, to.port.toShort(), Ops.data(a, data), data.size.toLong(), t) }) { }
    val port: Int get() = KnetNative.knet_udp_port(native).toInt() and 0xffff
    fun shutdown() = KnetNative.knet_udp_close(native)
    override fun close() = cleanup.clean()
}
