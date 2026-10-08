using Koral.Net.Native;

namespace Koral.Net;

/// <summary>knet::ConnectOptions.</summary>
public sealed record ConnectOptions
{
    /// <summary>For the whole connect and TLS handshake; null: none.</summary>
    public TimeSpan? Timeout { get; init; } = TimeSpan.FromSeconds(10);
    /// <summary>Speak TLS over the connection.</summary>
    public TlsOptions? Tls { get; init; }
}

/// <summary>knet::TcpStream: a connected byte stream, TCP or TLS. Every wait is a Task; a failure throws <see cref="NetException"/>.</summary>
public sealed unsafe class TcpStream : IDisposable
{
    internal IntPtr Native { get; private set; }
    internal TcpStream(IntPtr native) => Native = native;
    ~TcpStream() => Dispose();

    public static Task<TcpStream> Connect(string host, ushort port, ConnectOptions? options = null)
    {
        options ??= new ConnectOptions();
        using var strings = new Ops.Strings();
        var tls = Ops.Tls(options.Tls, strings);
        IntPtr token;
        var op = KnetNative.knet_tcp_connect(host, port, Ops.Ms(options.Timeout), options.Tls is null ? null : &tls, &token);
        return Ops.Run(op, token, o => new TcpStream(KnetNative.knet_op_take_stream(o)));
    }

    /// <summary>What has arrived: at least a byte, at most <paramref name="maxBytes"/>.</summary>
    public Task<byte[]> ReadSome(int maxBytes = 65536, TimeSpan? timeout = null)
    {
        IntPtr token;
        var op = KnetNative.knet_stream_read_some(Native, (nuint)maxBytes, Ops.Ms(timeout), &token);
        return Ops.Run(op, token, Ops.Bytes);
    }
    public Task<byte[]> ReadExactly(int bytes, TimeSpan? timeout = null)
    {
        IntPtr token;
        var op = KnetNative.knet_stream_read_exactly(Native, (nuint)bytes, Ops.Ms(timeout), &token);
        return Ops.Run(op, token, Ops.Bytes);
    }
    /// <summary>Up to and including <paramref name="delimiter"/>: a line, an HTTP header.</summary>
    public Task<string> ReadUntil(string delimiter = "\n", int limit = 64 * 1024, TimeSpan? timeout = null)
    {
        IntPtr token;
        var op = KnetNative.knet_stream_read_until(Native, delimiter, (nuint)limit, Ops.Ms(timeout), &token);
        return Ops.Run(op, token, o => Net.Text(Ops.Bytes(o)));
    }
    public Task<byte[]> ReadToEnd(int limit = 64 * 1024 * 1024, TimeSpan? timeout = null)
    {
        IntPtr token;
        var op = KnetNative.knet_stream_read_to_end(Native, (nuint)limit, Ops.Ms(timeout), &token);
        return Ops.Run(op, token, Ops.Bytes);
    }
    public Task Write(ReadOnlySpan<byte> data, TimeSpan? timeout = null)
    {
        IntPtr token, op;
        fixed (byte* p = data) op = KnetNative.knet_stream_write(Native, p, (nuint)data.Length, Ops.Ms(timeout), &token);
        return Ops.Run(op, token);
    }
    public Task Write(string text, TimeSpan? timeout = null) => Write(Net.Bytes(text), timeout);

    public void Close() => KnetNative.knet_stream_close(Native);
    public bool Secure => KnetNative.knet_stream_secure(Native) != 0;
    public Endpoint Local { get { ushort p; var a = KnetNative.Text(KnetNative.knet_stream_local(Native, &p)); return new(a, p); } }
    public Endpoint Remote { get { ushort p; var a = KnetNative.Text(KnetNative.knet_stream_remote(Native, &p)); return new(a, p); } }

    /// <summary>Hands the native stream over (to a WebSocket): this object no longer owns it.</summary>
    internal IntPtr Release() { var n = Native; Native = IntPtr.Zero; GC.SuppressFinalize(this); return n; }

    public void Dispose()
    {
        if (Native == IntPtr.Zero) return;
        KnetNative.knet_stream_destroy(Native);
        Native = IntPtr.Zero;
        GC.SuppressFinalize(this);
    }
}

/// <summary>knet::TlsServerOptions: a TLS server's identity.</summary>
public sealed record TlsServerOptions(string CertificatePem, string PrivateKeyPem);

/// <summary>knet::TcpListener.</summary>
public sealed unsafe class TcpListener : IDisposable
{
    private IntPtr _native;
    private TcpListener(IntPtr native) => _native = native;
    ~TcpListener() => Dispose();

    /// <summary>Listens on <paramref name="port"/> (0: any free one, see <see cref="Port"/>).</summary>
    public static TcpListener Listen(ushort port, string address = "0.0.0.0", TlsServerOptions? tls = null)
    {
        var native = KnetNative.knet_listener_listen(port, address, tls?.CertificatePem, tls?.PrivateKeyPem);
        if (native == IntPtr.Zero) throw new NetException(ErrorCode.eNetwork, Koral.Native.KoralNative.LastError());
        return new TcpListener(native);
    }

    /// <summary>The next connection (after its TLS handshake, for a TLS listener; clients whose handshake fails are skipped).</summary>
    public Task<TcpStream> Accept()
    {
        IntPtr token;
        var op = KnetNative.knet_listener_accept(_native, &token);
        return Ops.Run(op, token, o => new TcpStream(KnetNative.knet_op_take_stream(o)));
    }

    public ushort Port => KnetNative.knet_listener_port(_native);
    public void Close() => KnetNative.knet_listener_close(_native);

    public void Dispose()
    {
        if (_native == IntPtr.Zero) return;
        KnetNative.knet_listener_destroy(_native);
        _native = IntPtr.Zero;
        GC.SuppressFinalize(this);
    }
}

/// <summary>knet::Datagram.</summary>
public sealed record Datagram(Endpoint From, byte[] Data);

/// <summary>knet::UdpSocket.</summary>
public sealed unsafe class UdpSocket : IDisposable
{
    private IntPtr _native;
    private UdpSocket(IntPtr native) => _native = native;
    ~UdpSocket() => Dispose();

    public static UdpSocket Bind(ushort port = 0, string address = "0.0.0.0")
    {
        var native = KnetNative.knet_udp_bind(port, address);
        if (native == IntPtr.Zero) throw new NetException(ErrorCode.eNetwork, Koral.Native.KoralNative.LastError());
        return new UdpSocket(native);
    }

    public Task<Datagram> Receive(TimeSpan? timeout = null)
    {
        IntPtr token;
        var op = KnetNative.knet_udp_receive(_native, Ops.Ms(timeout), &token);
        return Ops.Run(op, token, o =>
        {
            ushort p;
            var a = KnetNative.Text(KnetNative.knet_op_address(o, 0, &p));
            return new Datagram(new Endpoint(a, p), Ops.Bytes(o));
        });
    }

    public Task SendTo(Endpoint to, ReadOnlySpan<byte> data)
    {
        IntPtr token, op;
        fixed (byte* p = data) op = KnetNative.knet_udp_send_to(_native, to.Address, to.Port, p, (nuint)data.Length, &token);
        return Ops.Run(op, token);
    }

    public ushort Port => KnetNative.knet_udp_port(_native);
    public void Close() => KnetNative.knet_udp_close(_native);

    public void Dispose()
    {
        if (_native == IntPtr.Zero) return;
        KnetNative.knet_udp_destroy(_native);
        _native = IntPtr.Zero;
        GC.SuppressFinalize(this);
    }
}
