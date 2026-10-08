using Koral.Net;
using Koral.Tests.Native;

namespace Koral.Tests;

// koral-net from C#: everything over loopback, in one process.
public static partial class Cases
{
    private static T Sync<T>(Task<T> task) => task.GetAwaiter().GetResult();
    private static void Sync(Task task) => task.GetAwaiter().GetResult();

    public static void NetTcpUdpAndHttp()
    {
        using var listener = Koral.Net.TcpListener.Listen(0, "127.0.0.1");
        var server = Task.Run(async () =>
        {
            using var client = await listener.Accept();
            var head = await client.ReadUntil("\r\n\r\n");
            await client.Write($"HTTP/1.1 200 OK\r\nContent-Length: 5\r\nX-Seen: {head.Split(' ')[1]}\r\n\r\nhello");
            client.Close();
        });
        var response = Sync(Http.Get($"http://127.0.0.1:{listener.Port}/path?q=1"));
        Sync(server);
        Check.Equal(200, response.Status, "status");
        Check.Equal("hello", response.Text, "body");
        Check.Equal("/path?q=1", response.Header("x-seen"), "the request's target, seen by the server");

        // A raw stream both ways, and a read that times out.
        var echo = Task.Run(async () =>
        {
            using var client = await listener.Accept();
            await client.Write(await client.ReadExactly(3));
        });
        using var stream = Sync(Koral.Net.TcpStream.Connect("127.0.0.1", listener.Port));
        Sync(stream.Write(Net.Net.Bytes("abc")));
        Check.Equal("abc", Net.Net.Text(Sync(stream.ReadExactly(3))), "echoed");
        Sync(echo);
        var timeout = Check.Throws<NetException>(() => Sync(stream.ReadSome(16, TimeSpan.FromMilliseconds(50))), "a read past the end of what was sent");
        Check.That(timeout.Code is ErrorCode.eConnectionClosed or ErrorCode.eTimedOut, $"closed or timed out ({timeout.Code})");

        using var a = UdpSocket.Bind(0, "127.0.0.1");
        using var b = UdpSocket.Bind(0, "127.0.0.1");
        var receiving = b.Receive(TimeSpan.FromSeconds(2));
        Sync(a.SendTo(new Endpoint("127.0.0.1", b.Port), Net.Net.Bytes("datagram")));
        var packet = Sync(receiving);
        Check.Equal("datagram", Net.Net.Text(packet.Data), "a datagram");
        Check.Equal(a.Port, packet.From.Port, "from whom");

        var refused = Check.Throws<NetException>(() => Sync(Koral.Net.TcpStream.Connect("127.0.0.1", 1, new ConnectOptions { Timeout = TimeSpan.FromSeconds(2) })), "nobody there");
        Check.Equal(ErrorCode.eNetwork, refused.Code, "a refused connection");
    }

    public static void NetWebSocketEcho()
    {
        using var listener = Koral.Net.TcpListener.Listen(0, "127.0.0.1");
        var server = Task.Run(async () =>
        {
            using var socket = await Koral.Net.WebSocket.Accept(await listener.Accept());
            for (;;)
            {
                WebSocketMessage message;
                try { message = await socket.Receive(); } catch (NetException) { return socket.CloseCode; }
                if (message.Binary) await socket.Send(message.Data); else await socket.Send("echo " + message.Text);
            }
        });
        using var ws = Sync(Koral.Net.WebSocket.Connect($"ws://127.0.0.1:{listener.Port}/", new WebSocketOptions { Protocols = ["chat"] }));
        Check.Equal("chat", ws.Protocol, "the subprotocol");
        Sync(ws.Send("hi"));
        Check.Equal("echo hi", Sync(ws.Receive()).Text, "text");
        var blob = Enumerable.Range(0, 70000).Select(i => (byte)(i * 7)).ToArray();
        Sync(ws.Send(blob));
        var back = Sync(ws.Receive());
        Check.That(back.Binary && back.Data.SequenceEqual(blob), "binary, 64-bit length");
        Sync(ws.Close(4000, "bye"));
        Check.Throws<NetException>(() => Sync(ws.Receive()), "closed");
        Check.Equal((ushort)4000, ws.CloseCode, "the close code");
        Check.Equal((ushort)4000, Sync(server), "the server saw it too");
    }

    public static unsafe void NetBitsMatchNative()
    {
        var random = new Koral.Random(21);
        for (int trial = 0; trial < 200; ++trial)
        {
            var managed = new Koral.Net.BitWriter();
            var native = KnetNative.knet_bit_writer_create();
            for (int k = 0; k < 20; ++k)
            {
                switch (random.NextInt(0, 7))
                {
                    case 0: { int bits = random.NextInt(1, 65); ulong v = random.NextU64(); managed.WriteBits(v, bits); KnetNative.knet_bit_writer_write_bits(native, v, bits); break; }
                    case 1: { ulong v = random.NextU64() >> random.NextInt(0, 64); managed.WriteVarUInt(v); KnetNative.knet_bit_writer_write_var_uint(native, v); break; }
                    case 2: { long v = (long)random.NextU64() >> random.NextInt(0, 64); managed.WriteVarInt(v); KnetNative.knet_bit_writer_write_var_int(native, v); break; }
                    case 3: { float v = random.NextFloat(-1e4f, 1e4f); managed.WriteFloat(v); KnetNative.knet_bit_writer_write_float(native, v); break; }
                    case 4: { float v = random.NextFloat(-2f, 12f); int bits = random.NextInt(2, 24); managed.WriteQuantized(v, 0f, 10f, bits); KnetNative.knet_bit_writer_write_quantized(native, v, 0f, 10f, bits); break; }
                    case 5:
                    {
                        var s = new string(Enumerable.Range(0, random.NextInt(0, 20)).Select(_ => (char)random.NextInt('a', 'z' + 1)).ToArray());
                        managed.WriteString(s);
                        KnetNative.knet_bit_writer_write_string(native, s);
                        break;
                    }
                    case 6:
                    {
                        var q = random.Rotation();
                        managed.WriteQuat(q);
                        float* f = stackalloc float[4] { q.X, q.Y, q.Z, q.W };
                        KnetNative.knet_bit_writer_write_quat(native, f, 10);
                        break;
                    }
                }
            }
            nuint size;
            var data = new ReadOnlySpan<byte>(KnetNative.knet_bit_writer_data(native, &size), (int)size).ToArray();
            KnetNative.knet_bit_writer_destroy(native);
            if (!data.SequenceEqual(managed.ToArray())) { Check.That(false, $"bits differ in trial {trial}"); return; }
        }

        var w = new Koral.Net.BitWriter();
        w.WriteQuat(Quat.AngleAxis(1f, new Vec3(1, 2, 3)));
        w.WriteVarInt(-70000);
        var r = new Koral.Net.BitReader(w.ToArray());
        Check.That(KMath.SameRotation(r.ReadQuat(), Quat.AngleAxis(1f, new Vec3(1, 2, 3)), 2e-3f), "a quaternion, smallest three");
        Check.Equal(-70000L, r.ReadVarInt(), "a varint");
        r.ReadU32();
        Check.That(r.Failed, "past the end, failed");

        var baseState = Enumerable.Range(0, 300).Select(i => (byte)(i % 7)).ToArray();
        var state = (byte[])baseState.Clone();
        state[10] = 99; state[200] = 1;
        var delta = Delta.Encode(baseState, state);
        Check.That(delta.Length < 20, $"a small change, a small delta ({delta.Length})");
        Check.That(Delta.Decode(baseState, delta)!.SequenceEqual(state), "and back");
    }

    public static void NetHostAndReplication()
    {
        var lossy = new NetworkSimulation(TimeSpan.FromMilliseconds(10), TimeSpan.FromMilliseconds(10), 0.1f);
        using var serverHost = Host.Create(new HostOptions { Address = "127.0.0.1", Simulation = lossy });
        using var clientHost = Host.Create(new HostOptions { Address = "127.0.0.1", MaxPeers = 0, Simulation = lossy });
        var toServer = clientHost.Connect("127.0.0.1", serverHost.Port);
        using var replicator = new Replicator(serverHost);
        using var replicas = new ReplicaSet(clientHost, toServer);
        var messages = new List<string>();

        bool Pump(Func<bool> done, Action? each = null)
        {
            var end = DateTime.UtcNow.AddSeconds(15);
            while (DateTime.UtcNow < end)
            {
                each?.Invoke();
                foreach (var e in serverHost.Update())
                    if (!replicator.Handle(e) && e.Type == NetEventType.Message) messages.Add(Net.Net.Text(e.Data));
                foreach (var e in clientHost.Update()) replicas.Handle(e);
                if (done()) return true;
                Thread.Sleep(1);
            }
            return false;
        }

        Check.That(Pump(() => clientHost.Connected(toServer) && serverHost.Peers.Length == 1), "connected");
        for (int i = 0; i < 50; ++i) clientHost.Send(toServer, 0, Net.Net.Bytes($"m{i}"));
        Check.That(Pump(() => messages.Count == 50), "50 reliable messages through 10% loss");
        Check.That(messages.SequenceEqual(Enumerable.Range(0, 50).Select(i => $"m{i}")), "in order, once each");

        byte[] Encode(Vec3 p) { var w = new Koral.Net.BitWriter(); w.WriteVec3(p); return w.ToArray(); }
        var ids = Enumerable.Range(0, 10).Select(i => replicator.Spawn("Rock", Encode(new Vec3(i, 0, 0)))).ToArray();
        uint tick = 0;
        Check.That(Pump(() => replicas.Ids.Length == 10, () => replicator.SendSnapshot(++tick)), "ten replicas");
        replicator.SetState(ids[4], Encode(new Vec3(4, 5, 6)));
        Check.That(Pump(() => new Koral.Net.BitReader(replicas.Find(ids[4])!.State).ReadVec3() == new Vec3(4, 5, 6),
                        () => replicator.SendSnapshot(++tick)), "a change arrives");
        replicas.TakeChanges();
        replicator.Despawn(ids[0]);
        Check.That(Pump(() => replicas.Find(ids[0]) is null, () => replicator.SendSnapshot(++tick)), "a despawn arrives");
        Check.That(replicas.TakeChanges().Any(c => !c.Spawned && c.Id == ids[0]), "as a change");
        Check.That(clientHost.Stats(toServer).Rtt > 0f, "a round trip measured");
    }

    public static void NetPrediction()
    {
        var predictor = new Predictor<float, float>(0f, (x, v) => x + v);
        for (int i = 0; i < 10; ++i) predictor.Apply(1f);
        Check.Equal(10f, predictor.Current, "run ahead");
        predictor.Reconcile(4, 3.5f);   // the server disagrees about tick 4
        Check.Equal(9.5f, predictor.Current, "rewound and replayed");
        Check.Equal(6, predictor.Pending.Count, "six inputs unconfirmed");

        var buffer = new InputBuffer<int>();
        buffer.Add(2, 20);
        buffer.Add(1, 10);
        Check.That(buffer.Take(1, out var a) && a == 10, "in order");
        Check.That(buffer.Take(2, out var b) && b == 20, "and the next");
        Check.That(buffer.Take(3, out var c) && c == 20, "a missing one repeats the last");
        Check.Equal(1ul, buffer.Missed, "counted as missed");

        var clock = new TickClock(50.0);
        Check.Equal(0, clock.Advance(0.019), "not yet a tick");
        Check.Equal(1, clock.Advance(0.002), "now one");
        Check.Equal(8, clock.Advance(10.0), "a stall, capped");
    }
}
