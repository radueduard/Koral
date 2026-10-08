using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using Koral.Net.Native;

// Value types cross the module's P/Invokes as they are.
[assembly: DisableRuntimeMarshalling]

namespace Koral.Net;

/// <summary>
/// A network operation that failed: knet's kor::Error — Code is eNetwork, eTimedOut, eConnectionClosed or
/// eProtocol (or eInvalidArgument). Where C++ returns a kor::Result, C# throws this.
/// </summary>
public sealed class NetException(ErrorCode code, string message) : Exception(message)
{
    public ErrorCode Code { get; } = code;
}

/// <summary>knet::Endpoint: a numeric address and a port.</summary>
public readonly record struct Endpoint(string Address, ushort Port)
{
    public bool IsV6 => Address.Contains(':');
    public override string ToString() => IsV6 ? $"[{Address}]:{Port}" : $"{Address}:{Port}";
}

/// <summary>knet::TlsOptions: how a TLS client checks whom it talks to.</summary>
public sealed record TlsOptions
{
    /// <summary>Check the certificate against trusted roots and the name. Off only for testing.</summary>
    public bool VerifyPeer { get; init; } = true;
    /// <summary>The name to send and check; null: the host connected to.</summary>
    public string? ServerName { get; init; }
    /// <summary>Extra trusted roots, PEM: a private CA, a self-signed test server.</summary>
    public string? CaPem { get; init; }
}

/// <summary>An operation's native handle and token, turned into an awaitable result.</summary>
internal static class Ops
{
    public static async Task<T> Run<T>(IntPtr op, IntPtr token, Func<IntPtr, T> read)
    {
        try
        {
            await new Token(token);
            ThrowIfFailed(op);
            return read(op);
        }
        finally
        {
            KnetNative.knet_op_destroy(op);
        }
    }

    public static Task Run(IntPtr op, IntPtr token) => Run(op, token, _ => 0);

    private static unsafe void ThrowIfFailed(IntPtr op)
    {
        if (KnetNative.knet_op_failed(op) != 0)
            throw new NetException((ErrorCode)KnetNative.knet_op_error_code(op), KnetNative.Text(KnetNative.knet_op_error(op)));
    }

    public static unsafe byte[] Bytes(IntPtr op)
    {
        nuint size;
        var data = KnetNative.knet_op_bytes(op, &size);
        return new ReadOnlySpan<byte>(data, (int)size).ToArray();
    }

    public static uint Ms(TimeSpan? t) => t is { } v ? (uint)Math.Max(0, v.TotalMilliseconds) : 0;

    /// <summary>Strings for a native struct, freed together when the scope ends.</summary>
    internal sealed class Strings : IDisposable
    {
        private readonly List<IntPtr> _held = [];
        public IntPtr Add(string? s)
        {
            if (s is null) return IntPtr.Zero;
            var p = Marshal.StringToCoTaskMemUTF8(s);
            _held.Add(p);
            return p;
        }
        public unsafe IntPtr* Array(IReadOnlyList<string> items)
        {
            if (items.Count == 0) return null;
            var p = Marshal.AllocCoTaskMem(IntPtr.Size * items.Count);
            _held.Add(p);
            for (int i = 0; i < items.Count; ++i) Marshal.WriteIntPtr(p, i * IntPtr.Size, Add(items[i]));
            return (IntPtr*)p;
        }
        public void Dispose() { foreach (var p in _held) Marshal.FreeCoTaskMem(p); }
    }

    public static KnetTlsOptions Tls(TlsOptions? o, Strings s) => new()
    {
        verify_peer = KnetNative.Bool(o?.VerifyPeer ?? true),
        server_name = s.Add(o?.ServerName),
        ca_pem = s.Add(o?.CaPem),
    };
}

/// <summary>knet's free functions.</summary>
public static unsafe class Net
{
    /// <summary>Every address <paramref name="host"/> has, for <paramref name="port"/>.</summary>
    public static Task<Endpoint[]> Resolve(string host, ushort port)
    {
        IntPtr token;
        var op = KnetNative.knet_resolve(host, port, &token);
        return Ops.Run(op, token, o =>
        {
            var all = new Endpoint[(int)KnetNative.knet_op_count(o)];
            for (int i = 0; i < all.Length; ++i)
            {
                ushort p;
                var a = KnetNative.Text(KnetNative.knet_op_address(o, (nuint)i, &p));
                all[i] = new Endpoint(a, p);
            }
            return all;
        });
    }

    /// <summary>The bytes of a string, UTF-8.</summary>
    public static byte[] Bytes(string text) => System.Text.Encoding.UTF8.GetBytes(text);
    public static string Text(ReadOnlySpan<byte> bytes) => System.Text.Encoding.UTF8.GetString(bytes);
}
