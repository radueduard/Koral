package koral.net

import java.lang.foreign.Arena
import java.lang.foreign.MemorySegment
import java.lang.foreign.ValueLayout.ADDRESS
import kotlin.time.Duration
import kotlin.time.Duration.Companion.seconds
import koral.interop.Native
import koral.net.interop.KnetLayouts
import koral.net.interop.KnetNative

/** knet::HttpRequest. */
data class HttpRequest(
    val url: String,
    val method: String = "GET",
    val headers: List<Pair<String, String>> = emptyList(),
    val body: ByteArray = ByteArray(0),
    val timeout: Duration? = 30.seconds,
    val maxRedirects: Int = 5,
    val tls: TlsOptions? = null,
)

/** knet::HttpResponse. An error status is a response, not an exception. */
class HttpResponse(val status: Int, val headers: List<Pair<String, String>>, val body: ByteArray, val url: String) {
    val ok: Boolean get() = status in 200..299
    val text: String get() = textOf(body)
    /** The first header of that name (case-insensitive), or "". */
    fun header(name: String): String = headers.firstOrNull { it.first.equals(name, ignoreCase = true) }?.second ?: ""
}

/** knet's HTTP client: Fetch, HttpGet, HttpPost. */
object Http {
    suspend fun fetch(request: HttpRequest): HttpResponse = Ops.start({ a, t ->
        val native = Struct(a, KnetLayouts.KnetHttpRequest)
            .string("method", request.method).string("url", request.url)
            .address("header_names", Ops.strings(a, request.headers.map { it.first }))
            .address("header_values", Ops.strings(a, request.headers.map { it.second }))
            .long("header_count", request.headers.size.toLong())
            .address("body", Ops.data(a, request.body)).long("body_size", request.body.size.toLong())
            .int("timeout_ms", Ops.ms(request.timeout)).int("max_redirects", request.maxRedirects)
            .struct("tls", Ops.tls(a, request.tls))
        KnetNative.knet_http_fetch(native.segment, t)
    }) { op ->
        val headers = (0 until KnetNative.knet_op_header_count(op)).map { i ->
            (KnetNative.knet_op_header_name(op, i)) to (KnetNative.knet_op_header_value(op, i))
        }
        HttpResponse(KnetNative.knet_op_status(op), headers, Ops.bytes(op), (KnetNative.knet_op_url(op)))
    }

    suspend fun get(url: String, headers: List<Pair<String, String>> = emptyList()) = fetch(HttpRequest(url, headers = headers))
    suspend fun post(url: String, body: String, contentType: String = "application/json", headers: List<Pair<String, String>> = emptyList()) =
        fetch(HttpRequest(url, "POST", headers + ("Content-Type" to contentType), bytesOf(body)))
}

/** knet::WebSocketMessage. */
class WebSocketMessage(val binary: Boolean, val data: ByteArray) {
    val text: String get() = textOf(data)
}

/** knet::WebSocketOptions. */
data class WebSocketOptions(
    val headers: List<Pair<String, String>> = emptyList(),
    val protocols: List<String> = emptyList(),
    val timeout: Duration? = 10.seconds,
    val tls: TlsOptions? = null,
    val maxMessage: Int = 16 * 1024 * 1024,
)

/** knet::WebSocket: pings are answered by itself, a close is answered and ends [receive]. */
class WebSocket private constructor(private val native: MemorySegment) : AutoCloseable {
    private val cleanup = cleaner.register(this) { KnetNative.knet_websocket_destroy(native) }

    companion object {
        suspend fun connect(url: String, options: WebSocketOptions = WebSocketOptions()): WebSocket = Ops.start({ a, t ->
            val o = Struct(a, KnetLayouts.KnetWebSocketOptions)
                .address("header_names", Ops.strings(a, options.headers.map { it.first }))
                .address("header_values", Ops.strings(a, options.headers.map { it.second }))
                .long("header_count", options.headers.size.toLong())
                .address("protocols", Ops.strings(a, options.protocols)).long("protocol_count", options.protocols.size.toLong())
                .int("timeout_ms", Ops.ms(options.timeout)).struct("tls", Ops.tls(a, options.tls))
                .long("max_message", options.maxMessage.toLong())
            KnetNative.knet_websocket_connect(url, o.segment, t)
        }) { WebSocket(KnetNative.knet_op_take_websocket(it)) }

        /** The server side: reads the upgrade request from [stream] (which it takes over) and answers it. */
        suspend fun accept(stream: TcpStream, maxMessage: Int = 16 * 1024 * 1024): WebSocket =
            Ops.start({ _, t -> KnetNative.knet_websocket_accept(stream.release(), maxMessage.toLong(), t) }) { WebSocket(KnetNative.knet_op_take_websocket(it)) }
    }

    suspend fun send(text: String) = Ops.start({ _, t -> KnetNative.knet_websocket_send_text(native, text, t) }) { }
    suspend fun send(binary: ByteArray) = Ops.start({ a, t -> KnetNative.knet_websocket_send_binary(native, Ops.data(a, binary), binary.size.toLong(), t) }) { }
    /** The next message; throws (eConnectionClosed) once closed — [closeCode] says how. */
    suspend fun receive(): WebSocketMessage = Ops.start({ _, t -> KnetNative.knet_websocket_receive(native, t) }) { WebSocketMessage(KnetNative.knet_op_binary(it), Ops.bytes(it)) }
    suspend fun close(code: Int, reason: String = "") = Ops.start({ _, t -> KnetNative.knet_websocket_close(native, code.toShort(), reason, t) }) { }

    val open: Boolean get() = KnetNative.knet_websocket_open(native)
    val closeCode: Int get() = KnetNative.knet_websocket_close_code(native).toInt() and 0xffff
    val protocol: String get() = (KnetNative.knet_websocket_protocol(native))
    override fun close() = cleanup.clean()
}
