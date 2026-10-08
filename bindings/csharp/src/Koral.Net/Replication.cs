using Koral.Net.Native;

namespace Koral.Net;

/// <summary>knet::ReplicationOptions: the host channels replication uses (3 reliable, 4 sequenced by default).</summary>
public sealed record ReplicationOptions(byte ControlChannel = 3, byte SnapshotChannel = 4);

/// <summary>
/// knet::Replicator: the server's objects, each a type name and state bytes (serialize them as you like — a
/// <see cref="BitWriter"/>), sent to every client as deltas against what it acknowledged.
/// </summary>
public sealed unsafe class Replicator : IDisposable
{
    private IntPtr _native;
    private readonly Host _host;   // kept alive as long as this is

    public Replicator(Host host, ReplicationOptions? options = null)
    {
        options ??= new ReplicationOptions();
        _host = host;
        _native = KnetNative.knet_replicator_create(host.Native, options.ControlChannel, options.SnapshotChannel);
    }
    ~Replicator() => Dispose();

    public uint Spawn(string type, ReadOnlySpan<byte> state, uint owner = 0)
    {
        fixed (byte* p = state) return KnetNative.knet_replicator_spawn(_native, type, p, (nuint)state.Length, owner);
    }
    public void SetState(uint id, ReadOnlySpan<byte> state)
    {
        fixed (byte* p = state) KnetNative.knet_replicator_set_state(_native, id, p, (nuint)state.Length);
    }
    public void Despawn(uint id) => KnetNative.knet_replicator_despawn(_native, id);
    public int Count => (int)KnetNative.knet_replicator_count(_native);
    public void SendSnapshot(uint tick) => KnetNative.knet_replicator_send_snapshot(_native, tick);
    /// <summary>True for replication's own messages; anything else is the game's.</summary>
    public bool Handle(NetEvent e) => e.WithNative(n => KnetNative.knet_replicator_handle(_native, &n) != 0);
    public int LastSnapshotBytes(uint peer) => (int)KnetNative.knet_replicator_last_snapshot_bytes(_native, peer);

    public void Dispose()
    {
        if (_native == IntPtr.Zero) return;
        KnetNative.knet_replicator_destroy(_native);
        _native = IntPtr.Zero;
        GC.SuppressFinalize(this);
        GC.KeepAlive(_host);
    }
}

/// <summary>knet::Replica.</summary>
public sealed record Replica(uint Id, string Type, bool Owned, byte[] State, uint Tick);
/// <summary>knet::ReplicaChange.</summary>
public sealed record ReplicaChange(bool Spawned, uint Id, string Type);

/// <summary>knet::ReplicaSet: the client side.</summary>
public sealed unsafe class ReplicaSet : IDisposable
{
    private IntPtr _native;
    private readonly Host _host;

    public ReplicaSet(Host host, uint server, ReplicationOptions? options = null)
    {
        options ??= new ReplicationOptions();
        _host = host;
        _native = KnetNative.knet_replica_set_create(host.Native, server, options.ControlChannel, options.SnapshotChannel);
    }
    ~ReplicaSet() => Dispose();

    public bool Handle(NetEvent e) => e.WithNative(n => KnetNative.knet_replica_set_handle(_native, &n) != 0);

    public List<ReplicaChange> TakeChanges()
    {
        var n = KnetNative.knet_replica_set_take_changes(_native);
        var changes = new List<ReplicaChange>((int)n);
        for (nuint i = 0; i < n; ++i)
        {
            uint kind, id;
            byte* type;
            KnetNative.knet_replica_set_change(_native, i, &kind, &id, &type);
            changes.Add(new ReplicaChange(kind == 0, id, KnetNative.Text(type)));
        }
        return changes;
    }

    public Replica? Find(uint id)
    {
        byte* state, type;
        nuint size;
        uint tick;
        byte owned;
        if (KnetNative.knet_replica_set_find(_native, id, &state, &size, &tick, &owned, &type) == 0) return null;
        return new Replica(id, KnetNative.Text(type), owned != 0, new ReadOnlySpan<byte>(state, (int)size).ToArray(), tick);
    }

    public uint[] Ids
    {
        get
        {
            var n = (int)KnetNative.knet_replica_set_count(_native);
            var ids = new uint[n];
            for (int i = 0; i < n; ++i) ids[i] = KnetNative.knet_replica_set_id_at(_native, (nuint)i);
            return ids;
        }
    }
    public uint LatestTick => KnetNative.knet_replica_set_latest_tick(_native);

    /// <summary>The two states around <paramref name="tick"/>, and how far between them it lies.</summary>
    public (byte[] From, byte[] To, float Alpha)? StatesAround(uint id, float tick)
    {
        byte* from, to;
        nuint fromSize, toSize;
        float alpha;
        if (KnetNative.knet_replica_set_states_around(_native, id, tick, &from, &fromSize, &to, &toSize, &alpha) == 0) return null;
        return (new ReadOnlySpan<byte>(from, (int)fromSize).ToArray(), new ReadOnlySpan<byte>(to, (int)toSize).ToArray(), alpha);
    }

    public void Dispose()
    {
        if (_native == IntPtr.Zero) return;
        KnetNative.knet_replica_set_destroy(_native);
        _native = IntPtr.Zero;
        GC.SuppressFinalize(this);
        GC.KeepAlive(_host);
    }
}

/// <summary>knet::EncodeDelta / DecodeDelta.</summary>
public static unsafe class Delta
{
    public static byte[] Encode(ReadOnlySpan<byte> baseState, ReadOnlySpan<byte> state)
    {
        fixed (byte* b = baseState) fixed (byte* s = state)
        {
            var size = KnetNative.knet_encode_delta(b, (nuint)baseState.Length, s, (nuint)state.Length, null, 0);
            var out_ = new byte[size];
            fixed (byte* o = out_) KnetNative.knet_encode_delta(b, (nuint)baseState.Length, s, (nuint)state.Length, o, size);
            return out_;
        }
    }
    public static byte[]? Decode(ReadOnlySpan<byte> baseState, ReadOnlySpan<byte> delta)
    {
        fixed (byte* b = baseState) fixed (byte* d = delta)
        {
            var size = KnetNative.knet_decode_delta(b, (nuint)baseState.Length, d, (nuint)delta.Length, null, 0);
            if (size == nuint.MaxValue) return null;
            var out_ = new byte[size];
            fixed (byte* o = out_) KnetNative.knet_decode_delta(b, (nuint)baseState.Length, d, (nuint)delta.Length, o, size);
            return out_;
        }
    }
}
