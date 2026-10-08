package koral.net

import java.lang.foreign.Arena
import java.lang.foreign.MemorySegment
import java.lang.foreign.ValueLayout.ADDRESS
import java.lang.foreign.ValueLayout.JAVA_BOOLEAN
import java.lang.foreign.ValueLayout.JAVA_BYTE
import java.lang.foreign.ValueLayout.JAVA_FLOAT
import java.lang.foreign.ValueLayout.JAVA_INT
import java.lang.foreign.ValueLayout.JAVA_LONG
import koral.interop.Native
import koral.net.interop.KnetNative

/** knet::ReplicationOptions: the host channels replication uses (3 reliable, 4 sequenced by default). */
data class ReplicationOptions(val controlChannel: Int = 3, val snapshotChannel: Int = 4)

private fun bytesAt(data: MemorySegment, size: Long) = if (size == 0L) ByteArray(0) else data.reinterpret(size).toArray(JAVA_BYTE)

/** knet::Replicator: the server's objects as type names and state bytes, sent as deltas against what each client acknowledged. */
class Replicator(private val host: Host, options: ReplicationOptions = ReplicationOptions()) : AutoCloseable {
    private val native = KnetNative.knet_replicator_create(host.native, options.controlChannel.toByte(), options.snapshotChannel.toByte())
    private val cleanup = cleaner.register(this, native.let { n -> Runnable { KnetNative.knet_replicator_destroy(n) } })

    fun spawn(type: String, state: ByteArray, owner: Int = 0): Int =
        Arena.ofConfined().use { a -> KnetNative.knet_replicator_spawn(native, type, Ops.data(a, state), state.size.toLong(), owner) }
    fun setState(id: Int, state: ByteArray) = Arena.ofConfined().use { a -> KnetNative.knet_replicator_set_state(native, id, Ops.data(a, state), state.size.toLong()) }
    fun despawn(id: Int) = KnetNative.knet_replicator_despawn(native, id)
    val count: Int get() = KnetNative.knet_replicator_count(native).toInt()
    fun sendSnapshot(tick: Int) = KnetNative.knet_replicator_send_snapshot(native, tick)
    /** True for replication's own messages; anything else is the game's. */
    fun handle(event: NetEvent): Boolean = Arena.ofConfined().use { a -> KnetNative.knet_replicator_handle(native, event.native(a)) }
    fun lastSnapshotBytes(peer: Int): Int = KnetNative.knet_replicator_last_snapshot_bytes(native, peer).toInt()
    override fun close() { cleanup.clean(); java.lang.ref.Reference.reachabilityFence(host) }
}

/** knet::Replica. */
class Replica(val id: Int, val type: String, val owned: Boolean, val state: ByteArray, val tick: Int)
/** knet::ReplicaChange. */
data class ReplicaChange(val spawned: Boolean, val id: Int, val type: String)

/** knet::ReplicaSet: the client side. */
class ReplicaSet(private val host: Host, server: Int, options: ReplicationOptions = ReplicationOptions()) : AutoCloseable {
    private val native = KnetNative.knet_replica_set_create(host.native, server, options.controlChannel.toByte(), options.snapshotChannel.toByte())
    private val cleanup = cleaner.register(this, native.let { n -> Runnable { KnetNative.knet_replica_set_destroy(n) } })

    fun handle(event: NetEvent): Boolean = Arena.ofConfined().use { a -> KnetNative.knet_replica_set_handle(native, event.native(a)) }

    fun takeChanges(): List<ReplicaChange> = Arena.ofConfined().use { a ->
        val n = KnetNative.knet_replica_set_take_changes(native)
        val kind = a.allocate(JAVA_INT)
        val id = a.allocate(JAVA_INT)
        val type = a.allocate(ADDRESS)
        (0 until n).map { i ->
            KnetNative.knet_replica_set_change(native, i, kind, id, type)
            ReplicaChange(kind.get(JAVA_INT, 0) == 0, id.get(JAVA_INT, 0), Native.kString(type.get(ADDRESS, 0)))
        }
    }

    fun find(id: Int): Replica? = Arena.ofConfined().use { a ->
        val state = a.allocate(ADDRESS)
        val size = a.allocate(JAVA_LONG)
        val tick = a.allocate(JAVA_INT)
        val owned = a.allocate(JAVA_BOOLEAN)
        val type = a.allocate(ADDRESS)
        if (!KnetNative.knet_replica_set_find(native, id, state, size, tick, owned, type)) null
        else Replica(id, Native.kString(type.get(ADDRESS, 0)), owned.get(JAVA_BOOLEAN, 0), bytesAt(state.get(ADDRESS, 0), size.get(JAVA_LONG, 0)), tick.get(JAVA_INT, 0))
    }

    val ids: List<Int> get() = (0 until KnetNative.knet_replica_set_count(native)).map { KnetNative.knet_replica_set_id_at(native, it) }
    val latestTick: Int get() = KnetNative.knet_replica_set_latest_tick(native)

    /** The two states around [tick], and how far between them it lies. */
    fun statesAround(id: Int, tick: Float): Triple<ByteArray, ByteArray, Float>? = Arena.ofConfined().use { a ->
        val from = a.allocate(ADDRESS)
        val fromSize = a.allocate(JAVA_LONG)
        val to = a.allocate(ADDRESS)
        val toSize = a.allocate(JAVA_LONG)
        val alpha = a.allocate(JAVA_FLOAT)
        if (!KnetNative.knet_replica_set_states_around(native, id, tick, from, fromSize, to, toSize, alpha)) null
        else Triple(bytesAt(from.get(ADDRESS, 0), fromSize.get(JAVA_LONG, 0)), bytesAt(to.get(ADDRESS, 0), toSize.get(JAVA_LONG, 0)), alpha.get(JAVA_FLOAT, 0))
    }

    override fun close() { cleanup.clean(); java.lang.ref.Reference.reachabilityFence(host) }
}

/** knet::EncodeDelta / DecodeDelta. */
object Delta {
    fun encode(base: ByteArray, state: ByteArray): ByteArray = Arena.ofConfined().use { a ->
        val b = Ops.data(a, base)
        val s = Ops.data(a, state)
        val size = KnetNative.knet_encode_delta(b, base.size.toLong(), s, state.size.toLong(), MemorySegment.NULL, 0)
        val out = a.allocate(maxOf(1L, size))
        KnetNative.knet_encode_delta(b, base.size.toLong(), s, state.size.toLong(), out, size)
        bytesAt(out, size)
    }
    fun decode(base: ByteArray, delta: ByteArray): ByteArray? = Arena.ofConfined().use { a ->
        val b = Ops.data(a, base)
        val d = Ops.data(a, delta)
        val size = KnetNative.knet_decode_delta(b, base.size.toLong(), d, delta.size.toLong(), MemorySegment.NULL, 0)
        if (size == -1L) return@use null
        val out = a.allocate(maxOf(1L, size))
        KnetNative.knet_decode_delta(b, base.size.toLong(), d, delta.size.toLong(), out, size)
        bytesAt(out, size)
    }
}
