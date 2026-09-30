using System.Numerics;
using Koral.Native;

namespace Koral;

/// <summary>kor::Framebuffer: the images a render pass draws into.</summary>
/// <example>
/// <code>
/// var gbuffer = new Framebuffer.Builder()
///     .AddColor(new() { Name = "albedo", View = albedo, Clear = new Vector4(0, 0, 0, 1) })
///     .SetDepth(new() { Name = "depth", View = depth })
///     .Build();
/// </code>
/// </example>
public sealed unsafe class Framebuffer : Resource
{
    internal Framebuffer(IntPtr native) : base(native) { }

    /// <summary>kor::Framebuffer::Builder.</summary>
    public sealed class Builder() : Koral.Builder(KoralNative.koral_framebuffer_builder_new())
    {
        /// <summary>kor::Framebuffer::Builder::AttachmentSource: an image view, or an image (its own view).</summary>
        public readonly struct AttachmentSource
        {
            internal Resource? Resource { get; }
            private AttachmentSource(Resource resource) => Resource = resource;
            public static implicit operator AttachmentSource(Image image) => new(image);
            public static implicit operator AttachmentSource(ImageView view) => new(view);
        }

        /// <summary>kor::Framebuffer::Builder::ColorAttachment.</summary>
        public record struct ColorAttachment()
        {
            public string Name { get; init; } = "";
            public AttachmentSource View { get; init; }
            public AttachmentSource Resolve { get; init; }
            public ClearColor Clear { get; init; } = new Vector4(0, 0, 0, 1);
        }

        /// <summary>kor::Framebuffer::Builder::DepthStencilAttachment.</summary>
        public record struct DepthStencilAttachment()
        {
            public string Name { get; init; } = "";
            public AttachmentSource View { get; init; }
            public AttachmentSource Resolve { get; init; }
            public float Depth { get; init; } = 1f;
            public int Stencil { get; init; }
        }

        public Builder AddColor(ColorAttachment attachment)
        {
            var clear = attachment.Clear.Native;
            KoralNative.koral_framebuffer_builder_add_color(Native, attachment.Name, HandleOf(attachment.View.Resource),
                                                            HandleOf(attachment.Resolve.Resource), &clear);
            return this;
        }

        public Builder SetDepth(DepthStencilAttachment attachment) => SetDepthStencil(0, attachment);
        public Builder SetStencil(DepthStencilAttachment attachment) => SetDepthStencil(1, attachment);
        public Builder SetDepthStencil(DepthStencilAttachment attachment) => SetDepthStencil(2, attachment);

        private Builder SetDepthStencil(uint which, DepthStencilAttachment a)
        {
            KoralNative.koral_framebuffer_builder_set_depth_stencil(Native, which, a.Name, HandleOf(a.View.Resource), HandleOf(a.Resolve.Resource),
                                                                    a.Depth, a.Stencil);
            return this;
        }

        public Builder SetResolveMode(ResolveMode mode)
        {
            KoralNative.koral_framebuffer_builder_set_resolve_mode(Native, (uint)mode);
            return this;
        }

        public Framebuffer Build() => Built<Framebuffer>(KoralNative.koral_framebuffer_builder_build(Native));
    }

    private static T Checked<T>(T value)
    {
        KoralNative.Check();
        return value;
    }

    public bool IsDefault => Checked(KoralNative.koral_framebuffer_is_default(Handle)).AsBool();
    public uint ColorAttachmentCount => Checked(KoralNative.koral_framebuffer_color_attachment_count(Handle));
    public SampleCount Samples => (SampleCount)Checked(KoralNative.koral_framebuffer_samples(Handle));

    public UVec2 Extent
    {
        get
        {
            uint x, y;
            KoralNative.koral_framebuffer_extent(Handle, &x, &y);
            KoralNative.Check();
            return new UVec2(x, y);
        }
    }

    public ImageView? ColorAttachment(uint index) => Checked(Wrap<ImageView>(KoralNative.koral_framebuffer_color_attachment(Handle, index)));
    public bool HasDepthAttachment => Checked(KoralNative.koral_framebuffer_has_depth_attachment(Handle)).AsBool();
    public ImageView? DepthAttachment => Checked(Wrap<ImageView>(KoralNative.koral_framebuffer_depth_attachment(Handle)));
    public bool HasStencilAttachment => Checked(KoralNative.koral_framebuffer_has_stencil_attachment(Handle)).AsBool();
    public ImageView? StencilAttachment => Checked(Wrap<ImageView>(KoralNative.koral_framebuffer_stencil_attachment(Handle)));
    public bool HasResolveAttachments => Checked(KoralNative.koral_framebuffer_has_resolve_attachments(Handle)).AsBool();
    public ImageView? ResolveAttachment(uint index) => Checked(Wrap<ImageView>(KoralNative.koral_framebuffer_resolve_attachment(Handle, index)));
    public ImageView? AttachmentNamed(string name) => Checked(Wrap<ImageView>(KoralNative.koral_framebuffer_attachment_named(Handle, name)));
    public Image? ImageNamed(string name) => Checked(Wrap<Image>(KoralNative.koral_framebuffer_image_named(Handle, name)));
    public Image? ColorImage(uint index = 0) => Checked(Wrap<Image>(KoralNative.koral_framebuffer_color_image(Handle, index)));
    public Image? DepthImage => Checked(Wrap<Image>(KoralNative.koral_framebuffer_depth_image(Handle)));

    public IReadOnlyList<string> AttachmentNames
    {
        get
        {
            var count = Checked(KoralNative.koral_framebuffer_attachment_name_count(Handle));
            var names = new string[count];
            for (uint i = 0; i < count; ++i) names[i] = KoralNative.Text(KoralNative.koral_framebuffer_attachment_name(Handle, i));
            return names;
        }
    }

    public ClearColor ClearColorAt(uint index)
    {
        KoralClearColor color;
        KoralNative.koral_framebuffer_clear_color_at(Handle, index, &color);
        KoralNative.Check();
        return ClearColor.From(color);
    }

    public float ClearDepth => Checked(KoralNative.koral_framebuffer_clear_depth(Handle));
    public int ClearStencil => Checked(KoralNative.koral_framebuffer_clear_stencil(Handle));
    public ResolveMode ResolveMethod => (ResolveMode)Checked(KoralNative.koral_framebuffer_resolve_method(Handle));

    public void Resize(UVec2 extent)
    {
        KoralNative.koral_framebuffer_resize(Handle, extent.X, extent.Y);
        KoralNative.Check();
    }
}

/// <summary>kor::Mesh: vertex and index buffers, and how the vertices are laid out.</summary>
public sealed unsafe class Mesh : Resource
{
    internal Mesh(IntPtr native) : base(native) { }

    /// <summary>kor::Mesh::Builder.</summary>
    public sealed class Builder() : Koral.Builder(KoralNative.koral_mesh_builder_new())
    {
        public Builder SetVertexBuffer(uint binding, Buffer vertexBuffer)
        {
            KoralNative.koral_mesh_builder_set_vertex_buffer(Native, binding, vertexBuffer.Handle);
            return this;
        }

        public Builder SetIndexBuffer(Buffer indexBuffer, ChannelType indexType = ChannelType.eUInt)
        {
            KoralNative.koral_mesh_builder_set_index_buffer(Native, indexBuffer.Handle, (uint)indexType);
            return this;
        }

        public Builder SetVertexLayout(VertexLayout layout)
        {
            var builder = Native;
            layout.WithNative(native => KoralNative.koral_mesh_builder_set_vertex_layout(builder, (KoralVertexLayout*)native));
            return this;
        }

        public Mesh Build() => Built<Mesh>(KoralNative.koral_mesh_builder_build(Native));
    }

    public ulong VertexCount { get { var v = KoralNative.koral_mesh_vertex_count(Handle); KoralNative.Check(); return v; } }
    public bool HasIndexBuffer { get { var v = KoralNative.koral_mesh_has_index_buffer(Handle); KoralNative.Check(); return v.AsBool(); } }

    public uint? IndexCount
    {
        get
        {
            uint count;
            return KoralNative.koral_mesh_index_count(Handle, &count).AsBool() ? count : null;
        }
    }

    public ChannelType? IndexType
    {
        get
        {
            uint type;
            return KoralNative.koral_mesh_index_type(Handle, &type).AsBool() ? (ChannelType)type : null;
        }
    }

    public IReadOnlyList<Buffer> VertexBuffers
    {
        get
        {
            var count = KoralNative.koral_mesh_vertex_buffer_count(Handle);
            var buffers = new Buffer[count];
            for (uint i = 0; i < count; ++i) buffers[i] = Wrap<Buffer>(KoralNative.koral_mesh_vertex_buffer(Handle, i))!;
            return buffers;
        }
    }

    public Buffer? IndexBuffer => Wrap<Buffer>(KoralNative.koral_mesh_index_buffer(Handle));

    /// <summary>MakeBuffer(data, usage): a device-local buffer holding <paramref name="data"/>, for a mesh.</summary>
    public static Buffer MakeBuffer<T>(ReadOnlySpan<T> data, Buffer.Usage usage) where T : unmanaged
    {
        var final = usage | Buffer.Usage.eTransferDst | Buffer.Usage.eTransferSrc | Buffer.Usage.eStorage;
        if (Context.SupportsRayTracing) final |= Buffer.Usage.eAccelerationStructureInput;
        return new Buffer.Builder<T>().SetData(data).SetUsage(final).SetType(Buffer.Type.eDeviceLocal).Build();
    }

    public static Buffer MakeBuffer<T>(T[] data, Buffer.Usage usage) where T : unmanaged => MakeBuffer(new ReadOnlySpan<T>(data), usage);

    /// <summary>MakeBuffer&lt;T&gt;(count, usage): an empty one, of <paramref name="instanceCount"/> Ts.</summary>
    public static Buffer MakeBuffer<T>(ulong instanceCount, Buffer.Usage usage) where T : unmanaged =>
        new Buffer.Builder<T>()
            .SetInstanceCount((long)instanceCount)
            .SetUsage(usage | Buffer.Usage.eTransferDst | Buffer.Usage.eTransferSrc | Buffer.Usage.eStorage)
            .SetType(Buffer.Type.eDeviceLocal)
            .Build();
}

/// <summary>kor::AccelerationStructure: meshes (bottom level) or instances of them (top level), for ray tracing.</summary>
public sealed unsafe partial class AccelerationStructure : Resource
{
    internal AccelerationStructure(IntPtr native) : base(native) { }

    /// <summary>kor::AccelerationStructure::Geometry.</summary>
    public sealed record Geometry(Mesh Mesh, ulong FirstVertex = 0, ulong VertexCount = 0, ulong FirstIndex = 0, ulong IndexCount = 0);

    /// <summary>kor::AccelerationStructure::Instance.</summary>
    public sealed record Instance(AccelerationStructure Blas)
    {
        public Matrix4x4 Transform { get; init; } = Matrix4x4.Identity;
        public uint InstanceCustomIndex { get; init; }
        public uint HitGroupIndex { get; init; }
    }

    /// <summary>kor::AccelerationStructure::Builder.</summary>
    public sealed class Builder() : Koral.Builder(KoralNative.koral_acceleration_structure_builder_new())
    {
        public Builder AddMesh(Mesh mesh) { KoralNative.koral_acceleration_structure_builder_add_mesh(Native, mesh.Handle); return this; }

        public Builder AddGeometry(Geometry geometry)
        {
            KoralNative.koral_acceleration_structure_builder_add_geometry(Native, geometry.Mesh.Handle, geometry.FirstVertex, geometry.VertexCount,
                                                                          geometry.FirstIndex, geometry.IndexCount);
            return this;
        }

        public Builder AddInstance(Instance instance)
        {
            var transform = instance.Transform;
            KoralNative.koral_acceleration_structure_builder_add_instance(Native, instance.Blas.Handle, (float*)&transform,
                                                                          instance.InstanceCustomIndex, instance.HitGroupIndex);
            return this;
        }

        public AccelerationStructure Build() => Built<AccelerationStructure>(KoralNative.koral_acceleration_structure_builder_build(Native));
    }

    public Type StructureType => (Type)KoralNative.koral_acceleration_structure_structure_type(Handle);
}
