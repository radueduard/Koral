package koral.tests

import java.lang.foreign.Arena
import java.lang.foreign.ValueLayout.JAVA_BYTE
import java.lang.foreign.ValueLayout.JAVA_FLOAT
import java.lang.foreign.ValueLayout.JAVA_LONG
import kotlin.test.Test
import kotlin.test.assertContentEquals
import kotlin.test.assertEquals
import kotlin.test.assertFailsWith
import kotlin.test.assertNotNull
import kotlin.test.assertNull
import kotlin.test.assertTrue
import kotlin.time.Duration.Companion.milliseconds
import kotlin.time.Duration.Companion.seconds
import kotlinx.coroutines.async
import kotlinx.coroutines.runBlocking
import koral.ErrorCode
import koral.Quat
import koral.Random
import koral.Vec3
import koral.net.*
import koral.net.interop.KnetNative

// koral-net from Kotlin: everything over loopback, in one process, with no application.
class NetTest {
    @Test
    fun tcpUdpAndHttp(): Unit = runBlocking {
        TcpListener.listen(0, "127.0.0.1").use { listener ->
            val server = async {
                listener.accept().use { client ->
                    val head = client.readUntil("\r\n\r\n")
                    client.write("HTTP/1.1 200 OK\r\nContent-Length: 5\r\nX-Seen: ${head.split(' ')[1]}\r\n\r\nhello")
                    client.shutdown()
                }
            }
            val response = Http.get("http://127.0.0.1:${listener.port}/path?q=1")
            server.await()
            assertEquals(200, response.status)
            assertEquals("hello", response.text)
            assertEquals("/path?q=1", response.header("x-seen"))

            val echo = async { listener.accept().use { it.write(it.readExactly(3)) } }
            TcpStream.connect("127.0.0.1", listener.port).use { stream ->
                stream.write("abc")
                assertEquals("abc", textOf(stream.readExactly(3)))
                echo.await()
                val e = assertFailsWith<NetException> { stream.readSome(16, 50.milliseconds) }
                assertTrue(e.code == ErrorCode.eConnectionClosed || e.code == ErrorCode.eTimedOut, "closed or timed out: ${e.code}")
            }
        }
        UdpSocket.bind(0, "127.0.0.1").use { a ->
            UdpSocket.bind(0, "127.0.0.1").use { b ->
                val receiving = async { b.receive(2.seconds) }
                a.sendTo(Endpoint("127.0.0.1", b.port), bytesOf("datagram"))
                val packet = receiving.await()
                assertEquals("datagram", textOf(packet.data))
                assertEquals(a.port, packet.from.port)
            }
        }
        val refused = assertFailsWith<NetException> { TcpStream.connect("127.0.0.1", 1, ConnectOptions(timeout = 2.seconds)) }
        assertEquals(ErrorCode.eNetwork, refused.code)
    }

    @Test
    fun webSocketEcho(): Unit = runBlocking {
        TcpListener.listen(0, "127.0.0.1").use { listener ->
            val server = async {
                WebSocket.accept(listener.accept()).use { socket ->
                    while (true) {
                        val message = try { socket.receive() } catch (_: NetException) { break }
                        if (message.binary) socket.send(message.data) else socket.send("echo " + message.text)
                    }
                    socket.closeCode
                }
            }
            WebSocket.connect("ws://127.0.0.1:${listener.port}/", WebSocketOptions(protocols = listOf("chat"))).use { ws ->
                assertEquals("chat", ws.protocol)
                ws.send("hi")
                assertEquals("echo hi", ws.receive().text)
                val blob = ByteArray(70000) { (it * 7).toByte() }
                ws.send(blob)
                val back = ws.receive()
                assertTrue(back.binary)
                assertContentEquals(blob, back.data)
                ws.close(4000, "bye")
                assertFailsWith<NetException> { ws.receive() }
                assertEquals(4000, ws.closeCode)
            }
            assertEquals(4000, server.await())
        }
    }

    @Test
    fun bitsMatchNative() {
        val random = Random(21uL)
        repeat(200) { trial ->
            val managed = BitWriter()
            val native = KnetNative.knet_bit_writer_create()
            Arena.ofConfined().use { a ->
                repeat(20) {
                    when (random.nextInt(0, 7)) {
                        0 -> { val bits = random.nextInt(1, 65); val v = random.nextU64().toLong(); managed.writeBits(v, bits); KnetNative.knet_bit_writer_write_bits(native, v, bits) }
                        1 -> { val v = (random.nextU64() shr random.nextInt(0, 64)).toLong(); managed.writeVarUInt(v); KnetNative.knet_bit_writer_write_var_uint(native, v) }
                        2 -> { val v = random.nextU64().toLong() shr random.nextInt(0, 64); managed.writeVarInt(v); KnetNative.knet_bit_writer_write_var_int(native, v) }
                        3 -> { val v = random.nextFloat(-1e4f, 1e4f); managed.writeFloat(v); KnetNative.knet_bit_writer_write_float(native, v) }
                        4 -> { val v = random.nextFloat(-2f, 12f); val bits = random.nextInt(2, 24); managed.writeQuantized(v, 0f, 10f, bits); KnetNative.knet_bit_writer_write_quantized(native, v, 0f, 10f, bits) }
                        5 -> { val s = String(CharArray(random.nextInt(0, 20)) { ('a' + random.nextInt(0, 26)) }); managed.writeString(s); KnetNative.knet_bit_writer_write_string(native, s) }
                        else -> { val q = random.rotation(); managed.writeQuat(q); KnetNative.knet_bit_writer_write_quat(native, a.allocateFrom(JAVA_FLOAT, q.x, q.y, q.z, q.w), 10) }
                    }
                }
                val size = a.allocate(JAVA_LONG)
                val data = KnetNative.knet_bit_writer_data(native, size)
                val n = size.get(JAVA_LONG, 0)
                val nativeBytes = if (n == 0L) ByteArray(0) else data.reinterpret(n).toArray(JAVA_BYTE)
                KnetNative.knet_bit_writer_destroy(native)
                assertContentEquals(nativeBytes, managed.toByteArray(), "trial $trial")
            }
        }
        val w = BitWriter()
        w.writeQuat(Quat.angleAxis(1f, Vec3(1f, 2f, 3f)))
        w.writeVarInt(-70000)
        val r = BitReader(w.toByteArray())
        assertTrue(koral.sameRotation(r.readQuat(), Quat.angleAxis(1f, Vec3(1f, 2f, 3f)), 2e-3f))
        assertEquals(-70000L, r.readVarInt())
        r.readU32()
        assertTrue(r.failed)

        val base = ByteArray(300) { (it % 7).toByte() }
        val state = base.copyOf().also { it[10] = 99; it[200] = 1 }
        val delta = Delta.encode(base, state)
        assertTrue(delta.size < 20, "a small change, a small delta (${delta.size})")
        assertContentEquals(state, Delta.decode(base, delta))
    }

    @Test
    fun hostAndReplication() {
        val lossy = NetworkSimulation(10.milliseconds, 10.milliseconds, 0.1f)
        Host.create(HostOptions(address = "127.0.0.1", simulation = lossy)).use { serverHost ->
            Host.create(HostOptions(address = "127.0.0.1", maxPeers = 0, simulation = lossy)).use { clientHost ->
                val toServer = clientHost.connect("127.0.0.1", serverHost.port)
                Replicator(serverHost).use { replicator ->
                    ReplicaSet(clientHost, toServer).use { replicas ->
                        val messages = mutableListOf<String>()
                        fun pump(each: () -> Unit = {}, done: () -> Boolean): Boolean {
                            val end = System.nanoTime() + 15_000_000_000L
                            while (System.nanoTime() < end) {
                                each()
                                for (e in serverHost.update()) if (!replicator.handle(e) && e.type == NetEventType.Message) messages += textOf(e.data)
                                for (e in clientHost.update()) replicas.handle(e)
                                if (done()) return true
                                Thread.sleep(1)
                            }
                            return false
                        }
                        assertTrue(pump { clientHost.connected(toServer) && serverHost.peers.size == 1 }, "connected")
                        repeat(50) { clientHost.send(toServer, 0, bytesOf("m$it")) }
                        assertTrue(pump { messages.size == 50 }, "50 reliable messages through 10% loss")
                        assertEquals(List(50) { "m$it" }, messages)

                        fun encode(p: Vec3) = BitWriter().also { it.writeVec3(p) }.toByteArray()
                        val ids = List(10) { replicator.spawn("Rock", encode(Vec3(it.toFloat(), 0f, 0f))) }
                        var tick = 0
                        assertTrue(pump({ replicator.sendSnapshot(++tick) }) { replicas.ids.size == 10 }, "ten replicas")
                        replicator.setState(ids[4], encode(Vec3(4f, 5f, 6f)))
                        assertTrue(pump({ replicator.sendSnapshot(++tick) }) { BitReader(replicas.find(ids[4])!!.state).readVec3() == Vec3(4f, 5f, 6f) }, "a change")
                        replicas.takeChanges()
                        replicator.despawn(ids[0])
                        assertTrue(pump({ replicator.sendSnapshot(++tick) }) { replicas.find(ids[0]) == null }, "a despawn")
                        assertTrue(replicas.takeChanges().any { !it.spawned && it.id == ids[0] })
                        assertNotNull(replicas.find(ids[1]))
                        assertTrue(clientHost.stats(toServer).rtt > 0f)
                    }
                }
            }
        }
    }

    @Test
    fun prediction() {
        val predictor = Predictor<Float, Float>(0f, { x, v -> x + v })
        repeat(10) { predictor.apply(1f) }
        assertEquals(10f, predictor.current)
        predictor.reconcile(4, 3.5f)
        assertEquals(9.5f, predictor.current)
        assertEquals(6, predictor.pending.size)

        val buffer = InputBuffer<Int>()
        buffer.add(2, 20)
        buffer.add(1, 10)
        assertEquals(10, buffer.take(1))
        assertEquals(20, buffer.take(2))
        assertEquals(20, buffer.take(3))
        assertEquals(1L, buffer.missed)
        assertNull(InputBuffer<Int>().take(1))

        val clock = TickClock(50.0)
        assertEquals(0, clock.advance(0.019))
        assertEquals(1, clock.advance(0.002))
        assertEquals(8, clock.advance(10.0))
    }
}
