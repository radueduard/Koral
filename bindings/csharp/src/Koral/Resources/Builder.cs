using Koral.Native;

namespace Koral;

/// <summary>
/// kor::Builder: what every builder is. Each setter is the C++ one, called on a C++ builder, so what it
/// checks and what it warns about are the same; <c>Build()</c> makes the resource, poisoned if it cannot be
/// built. A builder can be built from more than once, and set again in between.
/// </summary>
public abstract class Builder : IDisposable
{
    private IntPtr _native;

    private protected Builder(IntPtr native) => _native = KoralNative.Check(native);

    ~Builder() => Release();

    internal IntPtr Native
    {
        get
        {
            ObjectDisposedException.ThrowIf(_native == IntPtr.Zero, this);
            return _native;
        }
    }

    /// <summary>Builder::HasErrors: whether something set so far will make the build fail.</summary>
    public bool HasErrors => KoralNative.koral_builder_has_errors(Native).AsBool();

    /// <summary>Frees the C++ builder now, rather than when the collector gets to it. Resources it built stay.</summary>
    public void Dispose()
    {
        Release();
        GC.SuppressFinalize(this);
    }

    // A builder holds configuration and refs, never GPU objects: freeing it from the finalizer is safe.
    private void Release()
    {
        if (_native == IntPtr.Zero) return;
        KoralNative.koral_builder_destroy(_native);
        _native = IntPtr.Zero;
    }
}
