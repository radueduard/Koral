using Koral.Net.Native;

namespace Koral.Net;

/// <summary>knet::HttpRequest.</summary>
public sealed record HttpRequest
{
    public string Method { get; init; } = "GET";
    public required string Url { get; init; }
    public IReadOnlyList<(string Name, string Value)> Headers { get; init; } = [];
    public byte[] Body { get; init; } = [];
    /// <summary>For the whole exchange, redirects included; null: none.</summary>
    public TimeSpan? Timeout { get; init; } = TimeSpan.FromSeconds(30);
    public int MaxRedirects { get; init; } = 5;
    public TlsOptions? Tls { get; init; }
}

/// <summary>knet::HttpResponse. An error status is a response, not an exception.</summary>
public sealed record HttpResponse(int Status, IReadOnlyList<(string Name, string Value)> Headers, byte[] Body, string Url)
{
    public bool Ok => Status is >= 200 and < 300;
    public string Text => Net.Text(Body);
    /// <summary>The first header of that name (case-insensitive), or "".</summary>
    public string Header(string name) => Headers.FirstOrDefault(h => string.Equals(h.Name, name, StringComparison.OrdinalIgnoreCase)).Value ?? "";
}

/// <summary>knet's HTTP client: knet::Fetch, HttpGet, HttpPost.</summary>
public static unsafe class Http
{
    public static Task<HttpResponse> Fetch(HttpRequest request)
    {
        using var strings = new Ops.Strings();
        var names = request.Headers.Select(h => h.Name).ToList();
        var values = request.Headers.Select(h => h.Value).ToList();
        IntPtr token, op;
        fixed (byte* body = request.Body)
        {
            var native = new KnetHttpRequest
            {
                method = strings.Add(request.Method),
                url = strings.Add(request.Url),
                header_names = strings.Array(names),
                header_values = strings.Array(values),
                header_count = (nuint)names.Count,
                body = body,
                body_size = (nuint)request.Body.Length,
                timeout_ms = Ops.Ms(request.Timeout),
                max_redirects = request.MaxRedirects,
                tls = Ops.Tls(request.Tls, strings),
            };
            op = KnetNative.knet_http_fetch(&native, &token);
        }
        return Ops.Run(op, token, o =>
        {
            var headers = new List<(string, string)>();
            for (nuint i = 0; i < KnetNative.knet_op_header_count(o); ++i)
                headers.Add((KnetNative.Text(KnetNative.knet_op_header_name(o, i)), KnetNative.Text(KnetNative.knet_op_header_value(o, i))));
            return new HttpResponse(KnetNative.knet_op_status(o), headers, Ops.Bytes(o), KnetNative.Text(KnetNative.knet_op_url(o)));
        });
    }

    public static Task<HttpResponse> Get(string url, IReadOnlyList<(string, string)>? headers = null) =>
        Fetch(new HttpRequest { Url = url, Headers = headers ?? [] });

    public static Task<HttpResponse> Post(string url, string body, string contentType = "application/json", IReadOnlyList<(string, string)>? headers = null) =>
        Fetch(new HttpRequest { Method = "POST", Url = url, Body = Net.Bytes(body), Headers = [.. headers ?? [], ("Content-Type", contentType)] });
}

/// <summary>knet::WebSocketMessage.</summary>
public sealed record WebSocketMessage(bool Binary, byte[] Data)
{
    public string Text => Net.Text(Data);
}

/// <summary>knet::WebSocketOptions.</summary>
public sealed record WebSocketOptions
{
    public IReadOnlyList<(string Name, string Value)> Headers { get; init; } = [];
    public IReadOnlyList<string> Protocols { get; init; } = [];
    public TimeSpan? Timeout { get; init; } = TimeSpan.FromSeconds(10);
    public TlsOptions? Tls { get; init; }
    public int MaxMessage { get; init; } = 16 * 1024 * 1024;
}

/// <summary>knet::WebSocket: pings are answered by itself, a close is answered and ends Receive.</summary>
public sealed unsafe class WebSocket : IDisposable
{
    private IntPtr _native;
    private WebSocket(IntPtr native) => _native = native;
    ~WebSocket() => Dispose();

    public static Task<WebSocket> Connect(string url, WebSocketOptions? options = null)
    {
        options ??= new WebSocketOptions();
        using var strings = new Ops.Strings();
        var names = options.Headers.Select(h => h.Name).ToList();
        var values = options.Headers.Select(h => h.Value).ToList();
        var native = new KnetWebSocketOptions
        {
            header_names = strings.Array(names),
            header_values = strings.Array(values),
            header_count = (nuint)names.Count,
            protocols = strings.Array(options.Protocols),
            protocol_count = (nuint)options.Protocols.Count,
            timeout_ms = Ops.Ms(options.Timeout),
            tls = Ops.Tls(options.Tls, strings),
            max_message = (nuint)options.MaxMessage,
        };
        IntPtr token;
        var op = KnetNative.knet_websocket_connect(url, &native, &token);
        return Ops.Run(op, token, o => new WebSocket(KnetNative.knet_op_take_websocket(o)));
    }

    /// <summary>The server side: reads the upgrade request from <paramref name="stream"/> (which it takes over) and answers it.</summary>
    public static Task<WebSocket> Accept(TcpStream stream, int maxMessage = 16 * 1024 * 1024)
    {
        IntPtr token;
        var op = KnetNative.knet_websocket_accept(stream.Release(), (nuint)maxMessage, &token);
        return Ops.Run(op, token, o => new WebSocket(KnetNative.knet_op_take_websocket(o)));
    }

    public Task Send(string text)
    {
        IntPtr token;
        var op = KnetNative.knet_websocket_send_text(_native, text, &token);
        return Ops.Run(op, token);
    }
    public Task Send(ReadOnlySpan<byte> binary)
    {
        IntPtr token, op;
        fixed (byte* p = binary) op = KnetNative.knet_websocket_send_binary(_native, p, (nuint)binary.Length, &token);
        return Ops.Run(op, token);
    }
    /// <summary>The next message; throws (eConnectionClosed) once the socket is closed — <see cref="CloseCode"/> says how.</summary>
    public Task<WebSocketMessage> Receive()
    {
        IntPtr token;
        var op = KnetNative.knet_websocket_receive(_native, &token);
        return Ops.Run(op, token, o => new WebSocketMessage(KnetNative.knet_op_binary(o) != 0, Ops.Bytes(o)));
    }
    public Task Close(ushort code = 1000, string reason = "")
    {
        IntPtr token;
        var op = KnetNative.knet_websocket_close(_native, code, reason, &token);
        return Ops.Run(op, token);
    }

    public bool Open => KnetNative.knet_websocket_open(_native) != 0;
    public ushort CloseCode => KnetNative.knet_websocket_close_code(_native);
    public string Protocol => KnetNative.Text(KnetNative.knet_websocket_protocol(_native));

    public void Dispose()
    {
        if (_native == IntPtr.Zero) return;
        KnetNative.knet_websocket_destroy(_native);
        _native = IntPtr.Zero;
        GC.SuppressFinalize(this);
    }
}
