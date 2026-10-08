using System.Runtime.InteropServices;
using Koral.Native;

namespace Koral;

/// <summary>kor::Image: a texture, a render target, a storage image.</summary>
/// <example>
/// <code>
/// var albedo = new Image.Builder()
///     .SetFormat(Image.Format.eRGBA8_SRGB)
///     .SetExtent(new UVec2(width, height))
///     .SetData(pixels)
///     .SetUsage(Image.Usage.eSampled | Image.Usage.eTransferDst)
///     .Build();
/// </code>
/// </example>
public sealed unsafe partial class Image : Resource
{
    internal Image(IntPtr native) : base(native) { }

    /// <summary>kor::Image::Builder.</summary>
    public sealed class Builder : Koral.Builder
    {
        public Builder() : base(KoralNative.koral_image_builder_new()) { }

        public Builder SetIsPerFrame(bool value) { KoralNative.koral_image_builder_set_is_per_frame(Native, KoralNative.Bool(value)); return this; }
        public Builder SetSharedAcrossQueues(bool shared) { KoralNative.koral_image_builder_set_shared_across_queues(Native, KoralNative.Bool(shared)); return this; }
        public Builder SetType(Type type) { KoralNative.koral_image_builder_set_type(Native, (uint)type); return this; }
        public Builder SetFormat(Format format) { KoralNative.koral_image_builder_set_format(Native, (uint)format); return this; }
        /// <summary>The same extent in every dimension.</summary>
        public Builder SetExtent(uint extent) { KoralNative.koral_image_builder_set_extent(Native, extent, extent, extent); return this; }
        public Builder SetExtent(UVec2 extent) { KoralNative.koral_image_builder_set_extent(Native, extent.X, extent.Y, 1); return this; }
        public Builder SetExtent(UVec3 extent) { KoralNative.koral_image_builder_set_extent(Native, extent.X, extent.Y, extent.Z); return this; }
        public Builder SetMipLevels(uint mipLevels) { KoralNative.koral_image_builder_set_mip_levels(Native, mipLevels); return this; }
        public Builder SetArrayLayers(uint arrayLayers) { KoralNative.koral_image_builder_set_array_layers(Native, arrayLayers); return this; }
        public Builder SetSampleCount(SampleCount sampleCount) { KoralNative.koral_image_builder_set_sample_count(Native, (uint)sampleCount); return this; }
        public Builder SetUsage(Usage usage) { KoralNative.koral_image_builder_set_usage(Native, (uint)usage); return this; }

        /// <summary>Its pixels, copied now (and the transfer usages added, as in C++).</summary>
        public Builder SetData<T>(ReadOnlySpan<T> pixels) where T : unmanaged
        {
            fixed (T* pointer = pixels) KoralNative.koral_image_builder_set_data(Native, pointer, (ulong)(pixels.Length * sizeof(T)));
            return this;
        }

        public Builder SetData<T>(T[] pixels) where T : unmanaged => SetData(new ReadOnlySpan<T>(pixels));

        public Image Build() => Built<Image>(KoralNative.koral_image_builder_build(Native));
    }

    public void Resize(UVec3 extent)
    {
        KoralNative.koral_image_resize(Handle, extent.X, extent.Y, extent.Z);
        KoralNative.Check();
    }

    public ulong Generation => Checked(KoralNative.koral_image_generation(Handle));

    public UVec3 Extent
    {
        get
        {
            uint x, y, z;
            KoralNative.koral_image_extent(Handle, &x, &y, &z);
            KoralNative.Check();
            return new UVec3(x, y, z);
        }
    }

    public Type ImageType => (Type)Checked(KoralNative.koral_image_image_type(Handle));
    public Format PixelFormat => (Format)Checked(KoralNative.koral_image_pixel_format(Handle));
    public bool IsBgrOrder => Checked(KoralNative.koral_image_is_bgr_order(Handle)).AsBool();
    public SampleCount Samples => (SampleCount)Checked(KoralNative.koral_image_samples(Handle));
    public Usage UsageFlags => (Usage)Checked(KoralNative.koral_image_usage_flags(Handle));
    public uint MipLevels => Checked(KoralNative.koral_image_mip_levels(Handle));
    public uint ArrayLayers => Checked(KoralNative.koral_image_array_layers(Handle));
    public bool IsPerFrame => Checked(KoralNative.koral_image_is_per_frame(Handle)).AsBool();
    public bool IsSharedAcrossQueues => Checked(KoralNative.koral_image_is_shared_across_queues(Handle)).AsBool();
    public uint CopyIndex => Checked(KoralNative.koral_image_copy_index(Handle));
    public ImageShape NaturalShape => (ImageShape)Checked(KoralNative.koral_image_natural_shape(Handle));

    /// <summary>View(shape, coverage): the image's own view of that shape, kept by the image.</summary>
    public ImageView View(ImageShape shape = ImageShape.e2D, ViewCoverage coverage = ViewCoverage.eWholeImage)
    {
        var view = Wrap<ImageView>(KoralNative.koral_image_view(Handle, (uint)shape, (uint)coverage));
        KoralNative.Check();
        return view!;
    }

    public static uint ChannelSize(Format format) => KoralNative.koral_image_channel_size((uint)format);
    public static uint ChannelCount(Format format) => KoralNative.koral_image_channel_count((uint)format);
    public static bool IsFormatSupported(Format format, Usage usage = Usage.eSampled) => KoralNative.koral_image_is_format_supported((uint)format, (uint)usage).AsBool();
    public static bool IsBlockCompressed(Format format) => KoralNative.koral_image_is_block_compressed((uint)format).AsBool();
    public static UVec2 BlockExtent(Format format)
    {
        uint x, y;
        KoralNative.koral_image_block_extent((uint)format, &x, &y);
        return new UVec2(x, y);
    }
    public static uint BlockSize(Format format) => KoralNative.koral_image_block_size((uint)format);
    public static ulong SizeOfRegion(Format format, UVec3 extent, uint layerCount = 1) =>
        KoralNative.koral_image_size_of_region((uint)format, extent.X, extent.Y, extent.Z, layerCount);
    /// <summary>kor::IsDepthStencilFormat.</summary>
    public static bool IsDepthStencilFormat(Format format) => KoralNative.koral_is_depth_stencil_format((uint)format).AsBool();
    /// <summary>kor::IsStencilFormat.</summary>
    public static bool IsStencilFormat(Format format) => KoralNative.koral_is_stencil_format((uint)format).AsBool();

    private static T Checked<T>(T value)
    {
        KoralNative.Check();
        return value;
    }
}

/// <summary>kor::ImageView: a part of an image, seen as some shape.</summary>
public sealed unsafe partial class ImageView : Resource
{
    internal ImageView(IntPtr native) : base(native) { }

    /// <summary>kor::ImageView::ComponentMapping.</summary>
    public record struct ComponentMapping(Swizzle R = Swizzle.eIdentity, Swizzle G = Swizzle.eIdentity,
                                          Swizzle B = Swizzle.eIdentity, Swizzle A = Swizzle.eIdentity)
    {
        public ComponentMapping SetR(Swizzle swizzle) => this with { R = swizzle };
        public ComponentMapping SetG(Swizzle swizzle) => this with { G = swizzle };
        public ComponentMapping SetB(Swizzle swizzle) => this with { B = swizzle };
        public ComponentMapping SetA(Swizzle swizzle) => this with { A = swizzle };
    }

    /// <summary>kor::ImageView::Builder(image).</summary>
    public sealed class Builder(Image image) : Koral.Builder(KoralNative.koral_image_view_builder_new(image.Handle))
    {
        public Builder SetViewType(Type viewType) { KoralNative.koral_image_view_builder_set_view_type(Native, (uint)viewType); return this; }
        public Builder SetBaseMipLevel(uint level) { KoralNative.koral_image_view_builder_set_base_mip_level(Native, level); return this; }
        public Builder SetMipLevelCount(uint count) { KoralNative.koral_image_view_builder_set_mip_level_count(Native, count); return this; }
        public Builder SetBaseArrayLayer(uint layer) { KoralNative.koral_image_view_builder_set_base_array_layer(Native, layer); return this; }
        public Builder SetArrayLayerCount(uint count) { KoralNative.koral_image_view_builder_set_array_layer_count(Native, count); return this; }

        public Builder SetComponentMapping(ComponentMapping mapping)
        {
            var swizzle = stackalloc uint[] { (uint)mapping.R, (uint)mapping.G, (uint)mapping.B, (uint)mapping.A };
            KoralNative.koral_image_view_builder_set_component_mapping(Native, swizzle);
            return this;
        }

        public ImageView Build() => Built<ImageView>(KoralNative.koral_image_view_builder_build(Native));
    }

    public Image SourceImage => Checked(Wrap<Image>(KoralNative.koral_image_view_source_image(Handle)))!;
    public Type ViewType => (Type)Checked(KoralNative.koral_image_view_view_type(Handle));
    public uint BaseMipLevel => Checked(KoralNative.koral_image_view_base_mip_level(Handle));
    public uint MipLevelCount => Checked(KoralNative.koral_image_view_mip_level_count(Handle));
    public uint BaseArrayLayer => Checked(KoralNative.koral_image_view_base_array_layer(Handle));
    public uint ArrayLayerCount => Checked(KoralNative.koral_image_view_array_layer_count(Handle));
    public bool IsPerFrame => Checked(KoralNative.koral_image_view_is_per_frame(Handle)).AsBool();

    public ComponentMapping Components
    {
        get
        {
            var s = stackalloc uint[4];
            KoralNative.koral_image_view_components(Handle, s);
            KoralNative.Check();
            return new ComponentMapping((Swizzle)s[0], (Swizzle)s[1], (Swizzle)s[2], (Swizzle)s[3]);
        }
    }

    private static T Checked<T>(T value)
    {
        KoralNative.Check();
        return value;
    }
}

/// <summary>kor::Sampler: how an image is sampled.</summary>
public sealed partial class Sampler : Resource
{
    internal Sampler(IntPtr native) : base(native) { }

    /// <summary>kor::Sampler::Builder.</summary>
    public sealed class Builder() : Koral.Builder(KoralNative.koral_sampler_builder_new())
    {
        public Builder SetMinFilter(Filter filter) { KoralNative.koral_sampler_builder_set_min_filter(Native, (uint)filter); return this; }
        public Builder SetMagFilter(Filter filter) { KoralNative.koral_sampler_builder_set_mag_filter(Native, (uint)filter); return this; }
        public Builder SetMipmapMode(MipmapMode mode) { KoralNative.koral_sampler_builder_set_mipmap_mode(Native, (uint)mode); return this; }
        public Builder SetAddressModeU(AddressMode mode) { KoralNative.koral_sampler_builder_set_address_mode_u(Native, (uint)mode); return this; }
        public Builder SetAddressModeV(AddressMode mode) { KoralNative.koral_sampler_builder_set_address_mode_v(Native, (uint)mode); return this; }
        public Builder SetAddressModeW(AddressMode mode) { KoralNative.koral_sampler_builder_set_address_mode_w(Native, (uint)mode); return this; }
        public Builder SetMipLodBias(float bias) { KoralNative.koral_sampler_builder_set_mip_lod_bias(Native, bias); return this; }
        public Builder SetAnisotropyEnable(bool enable) { KoralNative.koral_sampler_builder_set_anisotropy_enable(Native, KoralNative.Bool(enable)); return this; }
        public Builder SetMaxAnisotropy(float anisotropy) { KoralNative.koral_sampler_builder_set_max_anisotropy(Native, anisotropy); return this; }
        public Builder SetCompareEnable(bool enable) { KoralNative.koral_sampler_builder_set_compare_enable(Native, KoralNative.Bool(enable)); return this; }
        public Builder SetCompareOp(CompareOp op) { KoralNative.koral_sampler_builder_set_compare_op(Native, (uint)op); return this; }
        public Builder SetMinLod(float lod) { KoralNative.koral_sampler_builder_set_min_lod(Native, lod); return this; }
        public Builder SetMaxLod(float lod) { KoralNative.koral_sampler_builder_set_max_lod(Native, lod); return this; }
        public Builder SetUnnormalizedCoordinates(bool value) { KoralNative.koral_sampler_builder_set_unnormalized_coordinates(Native, KoralNative.Bool(value)); return this; }
        public Sampler Build() => Built<Sampler>(KoralNative.koral_sampler_builder_build(Native));
    }
}

/// <summary>kor::BufferView: a buffer read as texels.</summary>
public sealed class BufferView : Resource
{
    internal BufferView(IntPtr native) : base(native) { }

    /// <summary>kor::BufferView::Builder(buffer).</summary>
    public sealed class Builder(Buffer buffer) : Koral.Builder(KoralNative.koral_buffer_view_builder_new(buffer.Handle))
    {
        public Builder SetFormat(Image.Format format) { KoralNative.koral_buffer_view_builder_set_format(Native, (uint)format); return this; }
        public Builder SetOffset(long offset) { KoralNative.koral_buffer_view_builder_set_offset(Native, offset); return this; }
        public Builder SetRange(long range) { KoralNative.koral_buffer_view_builder_set_range(Native, range); return this; }
        public BufferView Build() => Built<BufferView>(KoralNative.koral_buffer_view_builder_build(Native));
    }

    public Buffer SourceBuffer { get { var b = Wrap<Buffer>(KoralNative.koral_buffer_view_source_buffer(Handle)); KoralNative.Check(); return b!; } }
    public Image.Format PixelFormat { get { var v = KoralNative.koral_buffer_view_pixel_format(Handle); KoralNative.Check(); return (Image.Format)v; } }
    public long Offset { get { var v = KoralNative.koral_buffer_view_offset(Handle); KoralNative.Check(); return v; } }
    public long Range { get { var v = KoralNative.koral_buffer_view_range(Handle); KoralNative.Check(); return v; } }
    public ulong TexelCount { get { var v = KoralNative.koral_buffer_view_texel_count(Handle); KoralNative.Check(); return v; } }
}

/// <summary>kor::Shader: a stage of a pipeline, from a GLSL or Slang file, recompiled when the file changes.</summary>
public sealed unsafe partial class Shader : Resource
{
    internal Shader(IntPtr native) : base(native) { }

    /// <summary>kor::Shader::Builder.</summary>
    public sealed class Builder() : Koral.Builder(KoralNative.koral_shader_builder_new())
    {
        public Builder SetStage(Stage stage) { KoralNative.koral_shader_builder_set_stage(Native, (uint)stage); return this; }
        public Builder SetPath(string path) { KoralNative.koral_shader_builder_set_path(Native, path); return this; }
        public Builder SetEntryPoint(string entry) { KoralNative.koral_shader_builder_set_entry_point(Native, null, entry); return this; }
        public Builder SetEntryPoint(string module, string entry) { KoralNative.koral_shader_builder_set_entry_point(Native, module, entry); return this; }
        public Builder SetLang(Lang lang) { KoralNative.koral_shader_builder_set_lang(Native, (uint)lang); return this; }

        public Shader Build() => Built<Shader>(KoralNative.koral_shader_builder_build(Native));

        /// <summary>GetOrBuild(identifier): the repository's shader for this configuration, built the first time. Borrowed.</summary>
        public Shader GetOrBuild(string? identifier = null)
        {
            var shader = Wrap<Shader>(KoralNative.koral_shader_builder_get_or_build(Native, identifier));
            KoralNative.Check();
            return shader ?? throw new KoralException("GetOrBuild returned nothing");
        }
    }

    public Stage ShaderStage { get { var v = KoralNative.koral_shader_shader_stage(Handle); KoralNative.Check(); return (Stage)v; } }
    public Lang Language { get { var v = KoralNative.koral_shader_language(Handle); KoralNative.Check(); return (Lang)v; } }
    public string SourcePath { get { var v = KoralNative.Text(KoralNative.koral_shader_source_path(Handle)); KoralNative.Check(); return v; } }

    public static void AddSearchPath(string directory, bool front = false) => KoralNative.koral_shader_add_search_path(directory, KoralNative.Bool(front));

    /// <summary>A descriptor the shader declares, and where the compiler put it (kor::Shader::Descriptor).</summary>
    public sealed record Parameter(string Name, string BlockName, DescriptorType Type, uint Count, uint Set, uint Binding, AccessKind Access,
                                   ImageShape Shape, bool Active);
    /// <summary>One field of a push-constant block, flattened: <c>model</c>, <c>material.albedo</c> (kor::Shader::PushConstantField).</summary>
    public sealed record PushConstantField(string Name, uint Offset, uint Size, uint Scalar, uint Rows, uint Columns, uint Count, bool Aggregate);
    /// <summary>A specialization constant: its id, its name, and the bits of the value the shader gives it.</summary>
    public sealed record SpecializationConstant(string Name, uint Id, uint Scalar, uint Size, ulong DefaultValue);

    private static string Utf8(IntPtr text) => Marshal.PtrToStringUTF8(text) ?? "";

    /// <summary>Every descriptor the shader declares, by set and binding.</summary>
    public IReadOnlyList<Parameter> Parameters
    {
        get
        {
            var list = new List<Parameter>();
            for (nuint i = 0, n = KoralNative.koral_shader_parameter_count(Handle); i < n; ++i)
            {
                KoralShaderParameter p;
                if (KoralNative.koral_shader_parameter(Handle, i, &p) == 0) break;
                list.Add(new Parameter(Utf8(p.name), Utf8(p.block_name), (DescriptorType)p.type, p.count, p.set, p.binding,
                                       (AccessKind)p.access, (ImageShape)p.shape, p.active != 0));
            }
            KoralNative.Check();
            return list;
        }
    }

    /// <summary>Every field of its push-constant blocks.</summary>
    public IReadOnlyList<PushConstantField> PushConstants
    {
        get
        {
            var list = new List<PushConstantField>();
            for (nuint i = 0, n = KoralNative.koral_shader_push_constant_count(Handle); i < n; ++i)
            {
                KoralShaderPushConstant p;
                if (KoralNative.koral_shader_push_constant(Handle, i, &p) == 0) break;
                list.Add(new PushConstantField(Utf8(p.name), p.offset, p.size, p.scalar, p.rows, p.columns, p.count, p.aggregate != 0));
            }
            KoralNative.Check();
            return list;
        }
    }

    /// <summary>Its specialization constants, by id.</summary>
    public IReadOnlyList<SpecializationConstant> SpecializationConstants
    {
        get
        {
            var list = new List<SpecializationConstant>();
            for (nuint i = 0, n = KoralNative.koral_shader_specialization_constant_count(Handle); i < n; ++i)
            {
                KoralShaderSpecializationConstant c;
                if (KoralNative.koral_shader_specialization_constant(Handle, i, &c) == 0) break;
                list.Add(new SpecializationConstant(Utf8(c.name), c.id, c.scalar, c.size, c.default_value));
            }
            KoralNative.Check();
            return list;
        }
    }
}
