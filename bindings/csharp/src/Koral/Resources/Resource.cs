using Koral.Native;

namespace Koral;

/// <summary>
/// What every Koral resource is: kor::Resource&lt;T&gt; and kor::ResourceRef&lt;const T&gt; in one — a C#
/// reference is already a handle, so there is no wrapper to reach through.
/// </summary>
/// <remarks>
/// <para>
/// A resource a builder made is <see cref="Owned"/>: it lives until it is disposed — at the end of a
/// <c>using</c>, or with the scene that made it (made in a scene's constructor or any of its hooks, it is
/// that scene's), or with the application (made anywhere else). Anything else — an image a pass looks up, a window's framebuffer — is a borrowed
/// reference, like a ResourceRef: disposing it lets go of the reference, not the resource.
/// </para>
/// <para>
/// As in C++, a build that fails does not throw: it makes a <see cref="Poisoned"/> resource, which says
/// why (<see cref="Failure"/>), is logged once, and — for shaders and what is made from them — repairs
/// itself when the file is fixed. Using a poisoned resource's own members throws; passing it on to a
/// builder or a command poisons that, naming it as the cause.
/// </para>
/// <para>Dispose on the application's thread. There is no finalizer for an owned resource: freeing GPU
/// objects is not the garbage collector's job.</para>
/// </remarks>
public abstract unsafe class Resource : IDisposable, IEquatable<Resource>
{
    private IntPtr _native;
    private IResourceOwner? _owner;

    private protected Resource(IntPtr native)
    {
        _native = native;
        if (!KoralNative.koral_resource_owned(native).AsBool())
            return;   // borrowed: the finalizer lets go of the handle, which is safe from any thread
        GC.SuppressFinalize(this);
        if (SceneBridge.Constructing is { } constructing) constructing.Add(this);
        else if (Scene.Current is { } scene && scene.IsManaged) (_owner = scene).Own(this);
        else if (App.Current is { } app) (_owner = app).Own(this);
    }

    /// <summary>Taken over by <paramref name="owner"/> — a scene whose constructor made it.</summary>
    internal void OwnedBy(IResourceOwner owner)
    {
        _owner = owner;
        owner.Own(this);
    }

    ~Resource()
    {
        if (_native != IntPtr.Zero) KoralNative.koral_resource_release(_native);
    }

    /// <summary>The native handle, checked: a disposed resource refuses to be used.</summary>
    internal IntPtr Handle
    {
        get
        {
            ObjectDisposedException.ThrowIf(_native == IntPtr.Zero, this);
            return _native;
        }
    }

    internal static IntPtr HandleOf(Resource? resource) => resource?.Handle ?? IntPtr.Zero;

    public bool IsDisposed => _native == IntPtr.Zero;

    /// <summary>Whether this handle owns the resource (a Resource&lt;T&gt;) or borrows it (a ResourceRef).</summary>
    public bool Owned => !IsDisposed && KoralNative.koral_resource_owned(_native).AsBool();

    /// <summary>ResourceRef::Alive: the resource still exists, poisoned or not.</summary>
    public bool Alive => !IsDisposed && KoralNative.koral_resource_alive(_native).AsBool();

    /// <summary>Resource::Valid: it exists and is usable.</summary>
    public bool Valid => !IsDisposed && KoralNative.koral_resource_valid(_native).AsBool();

    /// <summary>Resource::Poisoned: its build failed; <see cref="Failure"/> says why.</summary>
    public bool Poisoned => !IsDisposed && KoralNative.koral_resource_poisoned(_native).AsBool();

    /// <summary>Resource::Failure: why it is poisoned, or null.</summary>
    public Error? Failure
    {
        get
        {
            if (!Poisoned) return null;
            return new Error((ErrorCode)KoralNative.koral_resource_error_code(Handle),
                             KoralNative.Text(KoralNative.koral_resource_error_message(Handle)),
                             KoralNative.Text(KoralNative.koral_resource_error_history(Handle)));
        }
    }

    /// <summary>Its name, for diagnostics; only the owner may set it.</summary>
    public string Name
    {
        get => KoralNative.Text(KoralNative.koral_resource_name(Handle));
        set
        {
            KoralNative.koral_resource_set_name(Handle, value);
            KoralNative.Check();
        }
    }

    /// <summary>Resource::Retry: builds a poisoned resource again, now. True when that worked.</summary>
    public bool Retry() => KoralNative.koral_resource_retry(Handle).AsBool();

    /// <summary>
    /// Frees an owned resource (with the GPU's use of it finished first, as Koral always does), or lets go
    /// of a borrowed one.
    /// </summary>
    public void Dispose()
    {
        if (_native == IntPtr.Zero) return;
        var native = _native;
        _native = IntPtr.Zero;
        _owner?.Disown(this);
        KoralNative.koral_resource_release(native);
        GC.SuppressFinalize(this);
    }

    /// <summary>The same resource — two handles, owned or borrowed, onto one object.</summary>
    public bool Equals(Resource? other) =>
        other is not null && !IsDisposed && !other.IsDisposed
        && KoralNative.koral_resource_identity(_native) == KoralNative.koral_resource_identity(other._native);

    public override bool Equals(object? obj) => obj is Resource other && Equals(other);
    public override int GetHashCode() => IsDisposed ? 0 : ((IntPtr)KoralNative.koral_resource_identity(_native)).GetHashCode();

    public override string ToString()
    {
        if (IsDisposed) return $"{GetType().Name} (disposed)";
        var name = Name;
        return $"{GetType().Name}{(name.Length > 0 ? $" '{name}'" : "")}{(Poisoned ? " (poisoned)" : "")}";
    }

    // ---- handles from Koral ------------------------------------------------------------------------------

    /// <summary>The C# object for a handle Koral returned: null for none.</summary>
    internal static T? Wrap<T>(IntPtr native) where T : Resource
    {
        if (native == IntPtr.Zero) return null;
        Resource made = (KoralResourceKind)KoralNative.koral_resource_kind(native) switch
        {
            KoralResourceKind.Buffer => new Buffer(native),
            KoralResourceKind.Image => new Image(native),
            KoralResourceKind.ImageView => new ImageView(native),
            KoralResourceKind.Sampler => new Sampler(native),
            KoralResourceKind.BufferView => new BufferView(native),
            KoralResourceKind.Shader => new Shader(native),
            KoralResourceKind.GraphicsPipeline => new GraphicsPipeline(native),
            KoralResourceKind.ComputePipeline => new ComputePipeline(native),
            KoralResourceKind.RayTracingPipeline => new RayTracingPipeline(native),
            KoralResourceKind.DescriptorSet => new DescriptorSet(native),
            KoralResourceKind.DescriptorSetLayout => new DescriptorSetLayout(native),
            KoralResourceKind.Framebuffer => new Framebuffer(native),
            KoralResourceKind.Mesh => new Mesh(native),
            KoralResourceKind.AccelerationStructure => new AccelerationStructure(native),
            _ => throw new KoralException("Koral returned a resource of a kind this binding does not know"),
        };
        return made as T ?? throw new KoralException($"expected a {typeof(T).Name}, got a {made.GetType().Name}");
    }

    /// <summary>What a builder built: owned, possibly poisoned, never null.</summary>
    internal static T Built<T>(IntPtr native) where T : Resource =>
        Wrap<T>(KoralNative.Check(native)) ?? throw new KoralException("nothing was built");
}

internal enum KoralResourceKind
{
    Buffer = 1, Image, ImageView, Sampler, BufferView, Shader, GraphicsPipeline, ComputePipeline, RayTracingPipeline,
    DescriptorSet, DescriptorSetLayout, Framebuffer, Mesh, AccelerationStructure,
}

internal static class NativeBool
{
    public static bool AsBool(this byte value) => value != 0;
}

/// <summary>What disposes resources it did not dispose itself: a scene, when it goes, or the application.</summary>
internal interface IResourceOwner
{
    void Own(IDisposable resource);
    void Disown(IDisposable resource);
}
