package koral.net

import java.lang.foreign.Arena
import java.lang.foreign.MemoryLayout.PathElement.groupElement
import java.lang.foreign.MemorySegment
import java.lang.foreign.ValueLayout.ADDRESS
import java.lang.foreign.ValueLayout.JAVA_BYTE
import java.lang.foreign.ValueLayout.JAVA_FLOAT
import java.lang.foreign.ValueLayout.JAVA_INT
import java.lang.foreign.ValueLayout.JAVA_LONG
import java.lang.foreign.ValueLayout.JAVA_SHORT
import kotlin.time.Duration
import kotlin.time.Duration.Companion.milliseconds
import kotlin.time.Duration.Companion.seconds
import koral.ErrorCode
import koral.interop.KoralNative
import koral.interop.Native
import koral.net.interop.KnetLayouts
import koral.net.interop.KnetNative

/** knet::Delivery. */
enum class Delivery { Reliable, Unreliable, UnreliableSequenced }
/** knet::EventType. */
enum class NetEventType { Connected, Disconnected, Message }
/** knet::DisconnectReason. */
enum class DisconnectReason { None, Local, Remote, TimedOut, Refused, Failed }

/** knet::NetworkSimulation: a bad network on demand. */
data class NetworkSimulation(val latency: Duration = Duration.ZERO, val jitter: Duration = Duration.ZERO, val loss: Float = 0f, val duplicate: Float = 0f)

/** knet::HostOptions. */
data class HostOptions(
    val port: Int = 0,
    val address: String = "0.0.0.0",
    val maxPeers: Int = 32,
    val protocolId: Long = 0,
    /** 0–2 for the game; 3 and 4 are replication's by default. */
    val channels: List<Delivery> = listOf(Delivery.Reliable, Delivery.Unreliable, Delivery.UnreliableSequenced, Delivery.Reliable, Delivery.UnreliableSequenced),
    val timeout: Duration = 10.seconds,
    val connectTimeout: Duration = 5.seconds,
    val keepAlive: Duration = 250.milliseconds,
    val maxMessage: Int = 1 shl 20,
    val simulation: NetworkSimulation = NetworkSimulation(),
)

/** knet::Event. */
class NetEvent(val type: NetEventType, val peer: Int, val channel: Int, val data: ByteArray, val reason: DisconnectReason) {
    internal fun native(a: Arena): MemorySegment = Struct(a, KnetLayouts.KnetEvent)
        .int("type", type.ordinal).int("peer", peer).int("channel", channel).int("reason", reason.ordinal)
        .address("data", Ops.data(a, data)).long("size", data.size.toLong()).segment
}

/** knet::PeerStats. */
data class PeerStats(val rtt: Float, val packetLoss: Float, val sentBytesPerSecond: Float, val receivedBytesPerSecond: Float,
                     val packetsSent: Long, val packetsReceived: Long, val packetsLost: Long)

/**
 * knet::Host: a game's connection protocol over UDP, both ends. Polled: [update] once per tick from one thread
 * receives, resends, sends, and returns what happened. Peer ids are Ints with the uint's bits.
 */
class Host private constructor(internal val native: MemorySegment) : AutoCloseable {
    private val cleanup = cleaner.register(this) { KnetNative.knet_host_destroy(native) }

    companion object {
        fun create(options: HostOptions = HostOptions()): Host = Arena.ofConfined().use { a ->
            val channels = a.allocateFrom(JAVA_BYTE, *options.channels.map { it.ordinal.toByte() }.toByteArray())
            val o = Struct(a, KnetLayouts.KnetHostOptions)
                .short("port", options.port).string("address", options.address).int("max_peers", options.maxPeers)
                .long("protocol_id", options.protocolId).address("channels", channels).long("channel_count", options.channels.size.toLong())
                .int("timeout_ms", Ops.ms(options.timeout)).int("connect_timeout_ms", Ops.ms(options.connectTimeout))
                .int("keep_alive_ms", Ops.ms(options.keepAlive)).long("max_message", options.maxMessage.toLong())
                .struct("simulation", simulation(a, options.simulation))
            val native = KnetNative.knet_host_create(o.segment)
            if (native == MemorySegment.NULL) throw NetException(ErrorCode.eNetwork, KoralNative.koral_last_error())
            Host(native)
        }

        private fun simulation(a: Arena, s: NetworkSimulation) = Struct(a, KnetLayouts.KnetNetworkSimulation)
            .int("latency_ms", Ops.ms(s.latency)).int("jitter_ms", Ops.ms(s.jitter)).float("loss", s.loss).float("duplicate", s.duplicate).segment
    }

    /** Starts connecting; Connected (or Disconnected: Failed, Refused) follows from [update]. */
    fun connect(host: String, port: Int): Int = KnetNative.knet_host_connect(native, host, port.toShort())
    fun send(peer: Int, channel: Int, message: ByteArray): Boolean =
        Arena.ofConfined().use { a -> KnetNative.knet_host_send(native, peer, channel.toByte(), Ops.data(a, message), message.size.toLong()) }
    fun broadcast(channel: Int, message: ByteArray, except: Int = 0) =
        Arena.ofConfined().use { a -> KnetNative.knet_host_broadcast(native, channel.toByte(), Ops.data(a, message), message.size.toLong(), except) }
    fun disconnect(peer: Int) = KnetNative.knet_host_disconnect(native, peer)

    fun update(): List<NetEvent> {
        val n = KnetNative.knet_host_update(native)
        return Arena.ofConfined().use { a ->
            val e = a.allocate(KnetLayouts.KnetEvent)
            val l = KnetLayouts.KnetEvent
            (0 until n).map { i ->
                KnetNative.knet_host_event(native, i, e)
                val size = e.get(JAVA_LONG, l.byteOffset(groupElement("size")))
                val data = e.get(ADDRESS, l.byteOffset(groupElement("data")))
                NetEvent(NetEventType.entries[e.get(JAVA_INT, l.byteOffset(groupElement("type")))],
                         e.get(JAVA_INT, l.byteOffset(groupElement("peer"))), e.get(JAVA_INT, l.byteOffset(groupElement("channel"))),
                         if (size == 0L) ByteArray(0) else data.reinterpret(size).toArray(JAVA_BYTE),
                         DisconnectReason.entries[e.get(JAVA_INT, l.byteOffset(groupElement("reason")))])
            }
        }
    }
    fun flush() = KnetNative.knet_host_flush(native)

    val peers: List<Int> get() = (0 until KnetNative.knet_host_peer_count(native)).map { KnetNative.knet_host_peer_at(native, it) }
    fun connected(peer: Int): Boolean = KnetNative.knet_host_connected(native, peer)
    fun address(peer: Int): Endpoint = Arena.ofConfined().use { a ->
        val p = a.allocate(JAVA_SHORT)
        val text = (KnetNative.knet_host_address(native, peer, p))
        Endpoint(text, p.get(JAVA_SHORT, 0).toInt() and 0xffff)
    }
    fun stats(peer: Int): PeerStats = Arena.ofConfined().use { a ->
        val s = KnetNative.knet_host_stats(a, native, peer)
        val l = KnetLayouts.KnetPeerStats
        fun f(n: String) = s.get(JAVA_FLOAT, l.byteOffset(groupElement(n)))
        fun j(n: String) = s.get(JAVA_LONG, l.byteOffset(groupElement(n)))
        PeerStats(f("rtt"), f("packet_loss"), f("sent_bytes_per_second"), f("received_bytes_per_second"), j("packets_sent"), j("packets_received"), j("packets_lost"))
    }
    val port: Int get() = KnetNative.knet_host_port(native).toInt() and 0xffff
    fun setSimulation(simulation: NetworkSimulation) = Arena.ofConfined().use { a -> KnetNative.knet_host_set_simulation(native, simulation(a, simulation)) }

    /** Says goodbye to every peer. */
    override fun close() = cleanup.clean()
}
