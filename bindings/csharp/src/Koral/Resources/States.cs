using System.Runtime.InteropServices;
using Koral.Native;

namespace Koral;

// kor::InputAssemblyState and the rest of a graphics pipeline's fixed state: the C++ structs, with the
// same defaults — `new DepthStencilState { DepthWriteEnable = false }` is `{ .depthWriteEnable = false }`.

/// <summary>kor::InputAssemblyState.</summary>
public record struct InputAssemblyState()
{
    public Topology Topology { get; init; } = Topology.eTriangleList;
    public bool PrimitiveRestartEnable { get; init; }

    internal readonly KoralInputAssemblyState Native => new() { topology = (uint)Topology, primitive_restart_enable = KoralNative.Bool(PrimitiveRestartEnable) };
}

/// <summary>kor::RasterizationState.</summary>
public record struct RasterizationState()
{
    public bool DepthClampEnable { get; init; }
    public bool RasterizerDiscardEnable { get; init; }
    public PolygonMode PolygonMode { get; init; } = PolygonMode.eFill;
    public CullMode CullMode { get; init; }
    public FrontFace FrontFace { get; init; } = FrontFace.eCounterClockwise;
    public bool DepthBiasEnable { get; init; }
    public float DepthBiasConstantFactor { get; init; }
    public float DepthBiasClamp { get; init; }
    public float DepthBiasSlopeFactor { get; init; }
    public float LineWidth { get; init; } = 1f;

    internal readonly KoralRasterizationState Native => new()
    {
        depth_clamp_enable = KoralNative.Bool(DepthClampEnable),
        rasterizer_discard_enable = KoralNative.Bool(RasterizerDiscardEnable),
        polygon_mode = (uint)PolygonMode,
        cull_mode = (uint)CullMode,
        front_face = (uint)FrontFace,
        depth_bias_enable = KoralNative.Bool(DepthBiasEnable),
        depth_bias_constant_factor = DepthBiasConstantFactor,
        depth_bias_clamp = DepthBiasClamp,
        depth_bias_slope_factor = DepthBiasSlopeFactor,
        line_width = LineWidth,
    };
}

/// <summary>kor::MultisampleState.</summary>
public record struct MultisampleState()
{
    public SampleCount SampleCount { get; init; } = SampleCount.e1;
    public bool SampleShadingEnable { get; init; }
    public float MinSampleShading { get; init; } = 1f;

    internal readonly KoralMultisampleState Native => new()
    {
        sample_count = (uint)SampleCount, sample_shading_enable = KoralNative.Bool(SampleShadingEnable), min_sample_shading = MinSampleShading,
    };
}

/// <summary>kor::StencilOpState.</summary>
public record struct StencilOpState()
{
    public StencilOp FailOp { get; init; } = StencilOp.eKeep;
    public StencilOp PassOp { get; init; } = StencilOp.eKeep;
    public StencilOp DepthFailOp { get; init; } = StencilOp.eKeep;
    public CompareOp CompareOp { get; init; } = CompareOp.eAlways;
    public uint CompareMask { get; init; }
    public uint WriteMask { get; init; }
    public uint Reference { get; init; }

    internal readonly KoralStencilOpState Native => new()
    {
        fail_op = (uint)FailOp, pass_op = (uint)PassOp, depth_fail_op = (uint)DepthFailOp, compare_op = (uint)CompareOp,
        compare_mask = CompareMask, write_mask = WriteMask, reference = Reference,
    };
}

/// <summary>kor::DepthStencilState.</summary>
public record struct DepthStencilState()
{
    public bool DepthTestEnable { get; init; } = true;
    public bool DepthWriteEnable { get; init; } = true;
    public CompareOp DepthCompareOp { get; init; } = CompareOp.eLess;
    public bool DepthBoundsEnable { get; init; }
    public bool StencilEnable { get; init; }
    public StencilOpState StencilFront { get; init; } = new();
    public StencilOpState StencilBack { get; init; } = new();
    public float MinDepth { get; init; }
    public float MaxDepth { get; init; } = 1f;

    internal readonly KoralDepthStencilState Native => new()
    {
        depth_test_enable = KoralNative.Bool(DepthTestEnable),
        depth_write_enable = KoralNative.Bool(DepthWriteEnable),
        depth_compare_op = (uint)DepthCompareOp,
        depth_bounds_enable = KoralNative.Bool(DepthBoundsEnable),
        stencil_enable = KoralNative.Bool(StencilEnable),
        stencil_front = StencilFront.Native,
        stencil_back = StencilBack.Native,
        min_depth = MinDepth,
        max_depth = MaxDepth,
    };
}

/// <summary>kor::ColorBlendState.</summary>
public sealed record ColorBlendState
{
    /// <summary>kor::ColorBlendState::AttachmentState.</summary>
    public record struct AttachmentState()
    {
        public bool BlendEnable { get; init; }
        public BlendFactor SrcColorBlendFactor { get; init; } = BlendFactor.eOne;
        public BlendFactor DstColorBlendFactor { get; init; } = BlendFactor.eZero;
        public BlendOp ColorBlendOp { get; init; } = BlendOp.eAdd;
        public BlendFactor SrcAlphaBlendFactor { get; init; } = BlendFactor.eOne;
        public BlendFactor DstAlphaBlendFactor { get; init; } = BlendFactor.eZero;
        public BlendOp AlphaBlendOp { get; init; } = BlendOp.eAdd;
        public ColorComponent ColorWriteMask { get; init; } = ColorComponent.eR | ColorComponent.eG | ColorComponent.eB | ColorComponent.eA;

        internal readonly KoralColorBlendAttachment Native => new()
        {
            blend_enable = KoralNative.Bool(BlendEnable),
            src_color_blend_factor = (uint)SrcColorBlendFactor, dst_color_blend_factor = (uint)DstColorBlendFactor,
            color_blend_op = (uint)ColorBlendOp,
            src_alpha_blend_factor = (uint)SrcAlphaBlendFactor, dst_alpha_blend_factor = (uint)DstAlphaBlendFactor,
            alpha_blend_op = (uint)AlphaBlendOp,
            color_write_mask = (uint)ColorWriteMask,
        };
    }

    public bool EnableLogicOp { get; init; }
    public LogicOp LogicOp { get; init; } = LogicOp.eCopy;
    public IReadOnlyList<AttachmentState> Attachments { get; init; } = [];
    public Vec4 BlendConstants { get; init; }
}

/// <summary>kor::TessellationState.</summary>
public sealed record TessellationState(Shader ControlShader, Shader EvalShader, uint PatchControlPoints = 3);

/// <summary>kor::VertexInputBindingDescription.</summary>
/// <remarks>InputRate eInstance: the buffer is stepped through an instance at a time — per-instance data.</remarks>
public record struct VertexInputBindingDescription(uint Binding, uint Stride, VertexInputRate InputRate = VertexInputRate.eVertex);

/// <summary>
/// kor::VertexLayout: how a mesh's buffers are laid out, attributes matched to a vertex shader's inputs by
/// semantic (or placed at a location).
/// </summary>
public sealed class VertexLayout
{
    /// <summary>kor::VertexLayout::Attribute.</summary>
    public sealed record Attribute
    {
        public string Semantic { get; init; } = "";
        public string SemanticNamespace { get; init; } = "";
        public uint Binding { get; init; }
        public uint Offset { get; init; }
        public ChannelType ChannelType { get; init; } = ChannelType.eFloat;
        public uint ChannelCount { get; init; }
        public uint? Location { get; init; }
        /// <summary>How many consecutive shader locations it fills: 4 for a mat4 (a column each), N for an array of N.</summary>
        public uint Locations { get; init; } = 1;
        /// <summary>The bytes from one of its locations to the next; 0: packed.</summary>
        public uint LocationStride { get; init; }

        /// <summary>A matrix of <paramref name="columns"/> columns of <paramref name="rows"/> floats, a location a column.</summary>
        public static Attribute Matrix(string semantic, uint binding, uint offset, uint columns = 4, uint rows = 4) =>
            new() { Semantic = semantic, Binding = binding, Offset = offset, ChannelType = ChannelType.eFloat, ChannelCount = rows, Locations = columns };

        public static Attribute AtLocation(uint location, uint binding, uint offset, ChannelType channelType, uint channelCount) =>
            new() { Location = location, Binding = binding, Offset = offset, ChannelType = channelType, ChannelCount = channelCount };
    }

    public List<VertexInputBindingDescription> Bindings { get; init; } = [];
    public List<Attribute> Attributes { get; init; } = [];
    /// <summary>Which attribute is the position (for acceleration structures and bounds), if any.</summary>
    public int? PositionAttribute { get; init; }

    public bool Empty => Bindings.Count == 0 && Attributes.Count == 0;

    /// <summary>The C form, valid inside <paramref name="use"/>: its strings and arrays are freed after.</summary>
    internal unsafe void WithNative(Action<IntPtr> use)
    {
        var bindings = new KoralVertexBinding[Bindings.Count];
        for (var i = 0; i < bindings.Length; ++i) bindings[i] = new KoralVertexBinding { binding = Bindings[i].Binding, stride = Bindings[i].Stride, input_rate = (uint)Bindings[i].InputRate };
        var attributes = new KoralVertexAttribute[Attributes.Count];
        try
        {
            for (var i = 0; i < attributes.Length; ++i)
            {
                var a = Attributes[i];
                attributes[i] = new KoralVertexAttribute
                {
                    semantic = KoralNative.Utf8(a.Semantic),
                    semantic_namespace = KoralNative.Utf8(a.SemanticNamespace),
                    binding = a.Binding,
                    offset = a.Offset,
                    channel_type = (uint)a.ChannelType,
                    channel_count = a.ChannelCount,
                    location = a.Location is { } l ? l : -1,
                    locations = a.Locations,
                    location_stride = a.LocationStride,
                };
            }
            fixed (KoralVertexBinding* b = bindings)
            fixed (KoralVertexAttribute* a = attributes)
            {
                var layout = new KoralVertexLayout
                {
                    bindings = b, binding_count = (nuint)bindings.Length,
                    attributes = a, attribute_count = (nuint)attributes.Length,
                    position_attribute = PositionAttribute ?? -1,
                };
                use((IntPtr)(&layout));
            }
        }
        finally
        {
            foreach (var a in attributes)
            {
                KoralNative.Free(a.semantic);
                KoralNative.Free(a.semantic_namespace);
            }
        }
    }
}

/// <summary>kor::ClearColor: a float, int or uint vector — converted from any of them, as the std::variant is.</summary>
[StructLayout(LayoutKind.Sequential)]
public readonly struct ClearColor
{
    private readonly uint _scalar, _components;
    private readonly UVec4 _bits;

    private ClearColor(uint scalar, uint components, UVec4 bits)
    {
        _scalar = scalar;
        _components = components;
        _bits = bits;
    }

    private static uint Bits(float f) => BitConverter.SingleToUInt32Bits(f);
    private static uint Bits(int i) => unchecked((uint)i);

    public static implicit operator ClearColor(float v) => new(0, 1, new UVec4(Bits(v), 0, 0, 0));
    public static implicit operator ClearColor(Vec2 v) => new(0, 2, new UVec4(Bits(v.X), Bits(v.Y), 0, 0));
    public static implicit operator ClearColor(Vec3 v) => new(0, 3, new UVec4(Bits(v.X), Bits(v.Y), Bits(v.Z), 0));
    public static implicit operator ClearColor(Vec4 v) => new(0, 4, new UVec4(Bits(v.X), Bits(v.Y), Bits(v.Z), Bits(v.W)));
    public static implicit operator ClearColor(int v) => new(1, 1, new UVec4(Bits(v), 0, 0, 0));
    public static implicit operator ClearColor(IVec2 v) => new(1, 2, new UVec4(Bits(v.X), Bits(v.Y), 0, 0));
    public static implicit operator ClearColor(IVec3 v) => new(1, 3, new UVec4(Bits(v.X), Bits(v.Y), Bits(v.Z), 0));
    public static implicit operator ClearColor(IVec4 v) => new(1, 4, new UVec4(Bits(v.X), Bits(v.Y), Bits(v.Z), Bits(v.W)));
    public static implicit operator ClearColor(uint v) => new(2, 1, new UVec4(v, 0, 0, 0));
    public static implicit operator ClearColor(UVec2 v) => new(2, 2, new UVec4(v.X, v.Y, 0, 0));
    public static implicit operator ClearColor(UVec3 v) => new(2, 3, new UVec4(v.X, v.Y, v.Z, 0));
    public static implicit operator ClearColor(UVec4 v) => new(2, 4, v);

    /// <summary>What it holds, as a Vec4 of floats (for a float colour).</summary>
    public Vec4 AsVector4() => new(BitConverter.UInt32BitsToSingle(_bits.X), BitConverter.UInt32BitsToSingle(_bits.Y),
                                      BitConverter.UInt32BitsToSingle(_bits.Z), BitConverter.UInt32BitsToSingle(_bits.W));

    internal unsafe KoralClearColor Native
    {
        get
        {
            var c = new KoralClearColor { scalar_type = _scalar, components = _components };
            c.u[0] = _bits.X; c.u[1] = _bits.Y; c.u[2] = _bits.Z; c.u[3] = _bits.W;
            return c;
        }
    }

    internal static unsafe ClearColor From(in KoralClearColor c) => new(c.scalar_type, c.components, new UVec4(c.u[0], c.u[1], c.u[2], c.u[3]));
}
