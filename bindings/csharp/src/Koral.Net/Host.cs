using Koral.Net.Native;

namespace Koral.Net;

/// <summary>knet::Delivery.</summary>
public enum Delivery : byte { Reliable, Unreliable, UnreliableSequenced }
/// <summary>knet::EventType.</summary>
public enum NetEventType : byte { Connected, Disconnected, Message }
/// <summary>knet::DisconnectReason.</summary>
public enum DisconnectReason : byte { None, Local, Remote, TimedOut, Refused, Failed }

/// <summary>knet::NetworkSimulation: a bad network on demand.</summary>
public sealed record NetworkSimulation(TimeSpan Latency = default, TimeSpan Jitter = default, float Loss = 0f, float Duplicate = 0f);

/// <summary>knet::HostOptions.</summary>
public sealed record HostOptions
{
    public ushort Port { get; init; }
    public string Address { get; init; } = "0.0.0.0";
    public uint MaxPeers { get; init; } = 32;
    public ulong ProtocolId { get; init; }
    /// <summary>0–2 for the game; 3 and 4 are replication's by default.</summary>
    public IReadOnlyList<Delivery> Channels { get; init; } =
        [Delivery.Reliable, Delivery.Unreliable, Delivery.UnreliableSequenced, Delivery.Reliable, Delivery.UnreliableSequenced];
    public TimeSpan Timeout { get; init; } = TimeSpan.FromSeconds(10);
    public TimeSpan ConnectTimeout { get; init; } = TimeSpan.FromSeconds(5);
    public TimeSpan KeepAlive { get; init; } = TimeSpan.FromMilliseconds(250);
    public int MaxMessage { get; init; } = 1 << 20;
    public NetworkSimulation Simulation { get; init; } = new();
}

/// <summary>knet::Event.</summary>
public sealed record NetEvent(NetEventType Type, uint Peer, byte Channel, byte[] Data, DisconnectReason Reason)
{
    internal unsafe T WithNative<T>(Func<KnetEvent, T> body)
    {
        fixed (byte* p = Data)
            return body(new KnetEvent { type = (uint)Type, peer = Peer, channel = Channel, reason = (uint)Reason, data = p, size = (nuint)Data.Length });
    }
}

/// <summary>knet::PeerStats.</summary>
public readonly record struct PeerStats(float Rtt, float PacketLoss, float SentBytesPerSecond, float ReceivedBytesPerSecond,
                                        ulong PacketsSent, ulong PacketsReceived, ulong PacketsLost);

/// <summary>
/// knet::Host: a game's connection protocol over UDP, both ends. Polled: <see cref="Update"/> once per tick from
/// one thread receives, resends, sends, and returns what happened.
/// </summary>
public sealed unsafe class Host : IDisposable
{
    internal IntPtr Native { get; private set; }
    private Host(IntPtr native) => Native = native;
    ~Host() => Dispose();

    public static Host Create(HostOptions? options = null)
    {
        options ??= new HostOptions();
        using var strings = new Ops.Strings();
        var channels = options.Channels.Select(c => (byte)c).ToArray();
        IntPtr native;
        fixed (byte* ch = channels)
        {
            var o = new KnetHostOptions
            {
                port = options.Port, address = strings.Add(options.Address), max_peers = options.MaxPeers, protocol_id = options.ProtocolId,
                channels = ch, channel_count = (nuint)channels.Length,
                timeout_ms = Ops.Ms(options.Timeout), connect_timeout_ms = Ops.Ms(options.ConnectTimeout), keep_alive_ms = Ops.Ms(options.KeepAlive),
                max_message = (nuint)options.MaxMessage, simulation = Sim(options.Simulation),
            };
            native = KnetNative.knet_host_create(&o);
        }
        if (native == IntPtr.Zero) throw new NetException(ErrorCode.eNetwork, Koral.Native.KoralNative.LastError());
        return new Host(native);
    }

    private static KnetNetworkSimulation Sim(NetworkSimulation s) =>
        new() { latency_ms = Ops.Ms(s.Latency), jitter_ms = Ops.Ms(s.Jitter), loss = s.Loss, duplicate = s.Duplicate };

    /// <summary>Starts connecting; Connected (or Disconnected: Failed, Refused) follows from Update.</summary>
    public uint Connect(string host, ushort port) => KnetNative.knet_host_connect(Native, host, port);
    public bool Send(uint peer, byte channel, ReadOnlySpan<byte> message)
    {
        fixed (byte* p = message) return KnetNative.knet_host_send(Native, peer, channel, p, (nuint)message.Length) != 0;
    }
    public void Broadcast(byte channel, ReadOnlySpan<byte> message, uint except = 0)
    {
        fixed (byte* p = message) KnetNative.knet_host_broadcast(Native, channel, p, (nuint)message.Length, except);
    }
    public void Disconnect(uint peer) => KnetNative.knet_host_disconnect(Native, peer);

    public List<NetEvent> Update()
    {
        var n = KnetNative.knet_host_update(Native);
        var events = new List<NetEvent>((int)n);
        for (nuint i = 0; i < n; ++i)
        {
            KnetEvent e;
            KnetNative.knet_host_event(Native, i, &e);
            events.Add(new NetEvent((NetEventType)e.type, e.peer, (byte)e.channel, new ReadOnlySpan<byte>(e.data, (int)e.size).ToArray(), (DisconnectReason)e.reason));
        }
        return events;
    }
    public void Flush() => KnetNative.knet_host_flush(Native);

    public uint[] Peers
    {
        get
        {
            var n = (int)KnetNative.knet_host_peer_count(Native);
            var peers = new uint[n];
            for (int i = 0; i < n; ++i) peers[i] = KnetNative.knet_host_peer_at(Native, (nuint)i);
            return peers;
        }
    }
    public bool Connected(uint peer) => KnetNative.knet_host_connected(Native, peer) != 0;
    public Endpoint Address(uint peer) { ushort p; var a = KnetNative.Text(KnetNative.knet_host_address(Native, peer, &p)); return new(a, p); }
    public PeerStats Stats(uint peer)
    {
        var s = KnetNative.knet_host_stats(Native, peer);
        return new PeerStats(s.rtt, s.packet_loss, s.sent_bytes_per_second, s.received_bytes_per_second, s.packets_sent, s.packets_received, s.packets_lost);
    }
    public ushort Port => KnetNative.knet_host_port(Native);
    public void SetSimulation(NetworkSimulation simulation) { var s = Sim(simulation); KnetNative.knet_host_set_simulation(Native, &s); }

    /// <summary>Says goodbye to every peer.</summary>
    public void Dispose()
    {
        if (Native == IntPtr.Zero) return;
        KnetNative.knet_host_destroy(Native);
        Native = IntPtr.Zero;
        GC.SuppressFinalize(this);
    }
}
