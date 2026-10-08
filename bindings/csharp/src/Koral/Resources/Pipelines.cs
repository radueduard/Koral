using System.Runtime.CompilerServices;
using Koral.Native;

namespace Koral;

/// <summary>kor::Pipeline: what the three kinds share — their descriptor-set layouts and push constants.</summary>
public abstract class Pipeline : Resource
{
    private protected Pipeline(IntPtr native) : base(native) { }

    /// <summary>SetLayoutRef(index): the layout of set <paramref name="index"/>, as the shaders declare it.</summary>
    public DescriptorSetLayout SetLayout(uint index)
    {
        var layout = Wrap<DescriptorSetLayout>(KoralNative.koral_pipeline_set_layout(Handle, index));
        KoralNative.Check();
        return layout!;
    }

    public bool UsesDeviceAddresses => KoralNative.koral_pipeline_uses_device_addresses(Handle).AsBool();

    /// <summary>Whether FindPushConstant(name) finds one: a push constant the shaders call <paramref name="name"/>.</summary>
    public bool HasPushConstant(string name) => KoralNative.koral_pipeline_has_push_constant(Handle, name).AsBool();

    private protected static unsafe void SpecializationConstant<T>(IntPtr builder, uint id, T value,
                                                                   delegate*<IntPtr, uint, void*, uint, void> set) where T : unmanaged
    {
        if (sizeof(T) != 4 && sizeof(T) != 8) throw new ArgumentException("a specialization constant is 4 or 8 bytes");
        set(builder, id, &value, (uint)sizeof(T));
    }

    private protected static unsafe void SpecializationConstant<T>(IntPtr builder, string name, T value,
                                                                   delegate*<IntPtr, string, void*, uint, void> set) where T : unmanaged
    {
        if (sizeof(T) != 4 && sizeof(T) != 8) throw new ArgumentException("a specialization constant is 4 or 8 bytes");
        set(builder, name, &value, (uint)sizeof(T));
    }
}

/// <summary>kor::GraphicsPipeline: vertex to fragment, rebuilt when one of its shaders is edited.</summary>
/// <example>
/// <code>
/// var pipeline = new GraphicsPipeline.Builder()
///     .SetVertexShader(new Shader.Builder().SetPath("mesh.vert").GetOrBuild(), layout)
///     .SetFragmentShader(new Shader.Builder().SetPath("mesh.frag").GetOrBuild())
///     .SetDepthStencilState(new DepthStencilState { DepthCompareOp = CompareOp.eLessOrEqual })
///     .Build();
/// </code>
/// </example>
public sealed unsafe class GraphicsPipeline : Pipeline
{
    internal GraphicsPipeline(IntPtr native) : base(native) { }

    /// <summary>kor::GraphicsPipeline::Builder.</summary>
    public sealed class Builder() : Koral.Builder(KoralNative.koral_graphics_pipeline_builder_new())
    {
        public Builder SetVertexShader(Shader shader)
        {
            KoralNative.koral_graphics_pipeline_builder_set_vertex_shader(Native, shader.Handle, null);
            return this;
        }

        public Builder SetVertexShader(Shader shader, VertexLayout layout)
        {
            var builder = Native;
            layout.WithNative(native => KoralNative.koral_graphics_pipeline_builder_set_vertex_shader(builder, shader.Handle, (KoralVertexLayout*)native));
            return this;
        }

        public Builder SetTessellationState(TessellationState state)
        {
            KoralNative.koral_graphics_pipeline_builder_set_tessellation_state(Native, state.ControlShader.Handle, state.EvalShader.Handle, state.PatchControlPoints);
            return this;
        }

        public Builder SetGeometryShader(Shader shader) { KoralNative.koral_graphics_pipeline_builder_set_geometry_shader(Native, shader.Handle); return this; }
        public Builder SetFragmentShader(Shader shader) { KoralNative.koral_graphics_pipeline_builder_set_fragment_shader(Native, shader.Handle); return this; }
        public Builder SetTaskShader(Shader shader) { KoralNative.koral_graphics_pipeline_builder_set_task_shader(Native, shader.Handle); return this; }
        public Builder SetMeshShader(Shader shader) { KoralNative.koral_graphics_pipeline_builder_set_mesh_shader(Native, shader.Handle); return this; }

        public Builder SetInputAssemblyState(InputAssemblyState state)
        {
            var native = state.Native;
            KoralNative.koral_graphics_pipeline_builder_set_input_assembly_state(Native, &native);
            return this;
        }

        public Builder SetRasterizationState(RasterizationState state)
        {
            var native = state.Native;
            KoralNative.koral_graphics_pipeline_builder_set_rasterization_state(Native, &native);
            return this;
        }

        public Builder SetMultisampleState(MultisampleState state)
        {
            var native = state.Native;
            KoralNative.koral_graphics_pipeline_builder_set_multisample_state(Native, &native);
            return this;
        }

        public Builder SetDepthStencilState(DepthStencilState state)
        {
            var native = state.Native;
            KoralNative.koral_graphics_pipeline_builder_set_depth_stencil_state(Native, &native);
            return this;
        }

        public Builder SetColorBlendState(ColorBlendState state)
        {
            var attachments = state.Attachments.Select(a => a.Native).ToArray();
            fixed (KoralColorBlendAttachment* pointer = attachments)
            {
                var native = new KoralColorBlendState
                {
                    enable_logic_op = KoralNative.Bool(state.EnableLogicOp),
                    logic_op = (uint)state.LogicOp,
                    attachments = pointer,
                    attachment_count = (nuint)attachments.Length,
                };
                native.blend_constants[0] = state.BlendConstants.X;
                native.blend_constants[1] = state.BlendConstants.Y;
                native.blend_constants[2] = state.BlendConstants.Z;
                native.blend_constants[3] = state.BlendConstants.W;
                KoralNative.koral_graphics_pipeline_builder_set_color_blend_state(Native, &native);
            }
            return this;
        }

        public Builder SetFramebuffer(Framebuffer framebuffer)
        {
            KoralNative.koral_graphics_pipeline_builder_set_framebuffer(Native, framebuffer.Handle);
            return this;
        }

        /// <summary>SetSpecializationConstant&lt;T&gt;(id, value): a 4- or 8-byte value.</summary>
        public Builder SetSpecializationConstant<T>(uint id, T value) where T : unmanaged
        {
            SpecializationConstant(Native, id, value, &SetConstant);
            return this;
        }

        /// <summary>SetSpecializationConstant(name, value): by the name its shader gave it.</summary>
        public Builder SetSpecializationConstant<T>(string name, T value) where T : unmanaged
        {
            SpecializationConstant(Native, name, value, &SetNamed);
            return this;
        }

        /// <summary>SetBinding(name, set, binding): puts the descriptor there, whatever its shader said.</summary>
        public Builder SetBinding(string name, uint set, uint binding)
        {
            KoralNative.koral_graphics_pipeline_builder_set_binding(Native, name, set, binding);
            return this;
        }

        private static void SetConstant(IntPtr b, uint id, void* v, uint n) => KoralNative.koral_graphics_pipeline_builder_set_specialization_constant(b, id, v, n);
        private static void SetNamed(IntPtr b, string name, void* v, uint n) => KoralNative.koral_graphics_pipeline_builder_set_specialization_constant_named(b, name, v, n);

        public GraphicsPipeline Build() => Built<GraphicsPipeline>(KoralNative.koral_graphics_pipeline_builder_build(Native));
    }
}

/// <summary>kor::ComputePipeline.</summary>
public sealed unsafe class ComputePipeline : Pipeline
{
    internal ComputePipeline(IntPtr native) : base(native) { }

    /// <summary>kor::ComputePipeline::Builder.</summary>
    public sealed class Builder() : Koral.Builder(KoralNative.koral_compute_pipeline_builder_new())
    {
        public Builder SetComputeShader(Shader shader)
        {
            KoralNative.koral_compute_pipeline_builder_set_compute_shader(Native, shader.Handle);
            return this;
        }

        public Builder SetSpecializationConstant<T>(uint id, T value) where T : unmanaged
        {
            SpecializationConstant(Native, id, value, &SetConstant);
            return this;
        }

        /// <summary>SetSpecializationConstant(name, value): by the name its shader gave it.</summary>
        public Builder SetSpecializationConstant<T>(string name, T value) where T : unmanaged
        {
            SpecializationConstant(Native, name, value, &SetNamed);
            return this;
        }

        /// <summary>SetBinding(name, set, binding): puts the descriptor there, whatever its shader said.</summary>
        public Builder SetBinding(string name, uint set, uint binding)
        {
            KoralNative.koral_compute_pipeline_builder_set_binding(Native, name, set, binding);
            return this;
        }

        private static void SetConstant(IntPtr b, uint id, void* v, uint n) => KoralNative.koral_compute_pipeline_builder_set_specialization_constant(b, id, v, n);
        private static void SetNamed(IntPtr b, string name, void* v, uint n) => KoralNative.koral_compute_pipeline_builder_set_specialization_constant_named(b, name, v, n);

        public ComputePipeline Build() => Built<ComputePipeline>(KoralNative.koral_compute_pipeline_builder_build(Native));
    }
}

/// <summary>kor::RayTracingPipeline.</summary>
public sealed unsafe class RayTracingPipeline : Pipeline
{
    internal RayTracingPipeline(IntPtr native) : base(native) { }

    /// <summary>kor::RayTracingPipeline::HitGroup: any of the three may be absent.</summary>
    public sealed record HitGroup(Shader? ClosestHitShader = null, Shader? AnyHitShader = null, Shader? IntersectionShader = null);

    /// <summary>kor::RayTracingPipeline::Builder.</summary>
    public sealed class Builder() : Koral.Builder(KoralNative.koral_ray_tracing_pipeline_builder_new())
    {
        public Builder SetRaygenShader(Shader shader) { KoralNative.koral_ray_tracing_pipeline_builder_set_raygen_shader(Native, shader.Handle); return this; }
        public Builder AddMissShader(Shader shader) { KoralNative.koral_ray_tracing_pipeline_builder_add_miss_shader(Native, shader.Handle); return this; }
        public Builder AddHitGroup(HitGroup group)
        {
            KoralNative.koral_ray_tracing_pipeline_builder_add_hit_group(Native, HandleOf(group.ClosestHitShader), HandleOf(group.AnyHitShader),
                                                                         HandleOf(group.IntersectionShader));
            return this;
        }
        public Builder AddCallableShader(Shader shader) { KoralNative.koral_ray_tracing_pipeline_builder_add_callable_shader(Native, shader.Handle); return this; }
        public Builder SetMaxRecursionDepth(uint depth) { KoralNative.koral_ray_tracing_pipeline_builder_set_max_recursion_depth(Native, depth); return this; }
        public Builder SetSpecializationConstant<T>(uint id, T value) where T : unmanaged { SpecializationConstant(Native, id, value, &SetConstant); return this; }
        /// <summary>SetSpecializationConstant(name, value): by the name its shader gave it.</summary>
        public Builder SetSpecializationConstant<T>(string name, T value) where T : unmanaged { SpecializationConstant(Native, name, value, &SetNamed); return this; }
        /// <summary>SetBinding(name, set, binding): puts the descriptor there, whatever its shader said.</summary>
        public Builder SetBinding(string name, uint set, uint binding) { KoralNative.koral_ray_tracing_pipeline_builder_set_binding(Native, name, set, binding); return this; }
        private static void SetConstant(IntPtr b, uint id, void* v, uint n) => KoralNative.koral_ray_tracing_pipeline_builder_set_specialization_constant(b, id, v, n);
        private static void SetNamed(IntPtr b, string name, void* v, uint n) => KoralNative.koral_ray_tracing_pipeline_builder_set_specialization_constant_named(b, name, v, n);
        public RayTracingPipeline Build() => Built<RayTracingPipeline>(KoralNative.koral_ray_tracing_pipeline_builder_build(Native));
    }

    public uint MaxRecursionDepth => KoralNative.koral_ray_tracing_pipeline_max_recursion_depth(Handle);
}

/// <summary>kor::DescriptorSetLayout: what a set holds, and where — made from a pipeline's shaders.</summary>
public sealed class DescriptorSetLayout : Resource
{
    internal DescriptorSetLayout(IntPtr native) : base(native) { }

    /// <summary>FindBinding(name): the binding the shaders call <paramref name="name"/>, or null.</summary>
    public uint? FindBinding(string name)
    {
        var binding = KoralNative.koral_descriptor_set_layout_find_binding(Handle, name);
        return binding < 0 ? null : (uint)binding;
    }
}

/// <summary>
/// kor::DescriptorSet: the resources a pipeline's shaders read, bound by name (as the shaders call them) or
/// by binding.
/// </summary>
/// <example>
/// <code>
/// var set = new DescriptorSet.Builder(pipeline, 0)
///     .Write("camera", cameraBuffer)
///     .Write("albedo", albedo, sampler)
///     .Build();
/// </code>
/// </example>
public sealed unsafe class DescriptorSet : Resource
{
    internal DescriptorSet(IntPtr native) : base(native) { }

    /// <summary>kor::DescriptorSet::Builder.</summary>
    public sealed class Builder : Koral.Builder
    {
        /// <summary>Builder(pipeline, setIndex): set <paramref name="setIndex"/> of <paramref name="pipeline"/>'s layout.</summary>
        public Builder(Pipeline pipeline, uint setIndex) : base(KoralNative.koral_descriptor_set_builder_new(pipeline.Handle, setIndex)) { }
        public Builder(DescriptorSetLayout layout) : base(KoralNative.koral_descriptor_set_builder_new(layout.Handle, 0)) { }

        public Builder Write(uint binding, Buffer buffer, uint index = 0) { KoralNative.koral_descriptor_set_builder_write_buffer(Native, null, binding, index, buffer.Handle); return this; }
        public Builder Write(string name, Buffer buffer) { KoralNative.koral_descriptor_set_builder_write_buffer(Native, name, 0, 0, buffer.Handle); return this; }
        public Builder Write(uint binding, Buffer.Slice slice, uint index = 0) { KoralNative.koral_descriptor_set_builder_write_buffer_slice(Native, null, binding, index, slice.Buffer.Handle, slice.Offset, slice.Size); return this; }
        public Builder Write(string name, Buffer.Slice slice) { KoralNative.koral_descriptor_set_builder_write_buffer_slice(Native, name, 0, 0, slice.Buffer.Handle, slice.Offset, slice.Size); return this; }
        public Builder Write(uint binding, BufferView view, uint index = 0) { KoralNative.koral_descriptor_set_builder_write_buffer_view(Native, null, binding, index, view.Handle); return this; }
        public Builder Write(string name, BufferView view) { KoralNative.koral_descriptor_set_builder_write_buffer_view(Native, name, 0, 0, view.Handle); return this; }
        public Builder Write(uint binding, Image image, uint index = 0) { KoralNative.koral_descriptor_set_builder_write_image(Native, null, binding, index, image.Handle, IntPtr.Zero); return this; }
        public Builder Write(string name, Image image) { KoralNative.koral_descriptor_set_builder_write_image(Native, name, 0, 0, image.Handle, IntPtr.Zero); return this; }
        public Builder Write(uint binding, Image image, Sampler sampler, uint index = 0) { KoralNative.koral_descriptor_set_builder_write_image(Native, null, binding, index, image.Handle, sampler.Handle); return this; }
        public Builder Write(string name, Image image, Sampler sampler) { KoralNative.koral_descriptor_set_builder_write_image(Native, name, 0, 0, image.Handle, sampler.Handle); return this; }
        public Builder Write(uint binding, ImageView view, uint index = 0) { KoralNative.koral_descriptor_set_builder_write_image_view(Native, null, binding, index, view.Handle, IntPtr.Zero); return this; }
        public Builder Write(string name, ImageView view) { KoralNative.koral_descriptor_set_builder_write_image_view(Native, name, 0, 0, view.Handle, IntPtr.Zero); return this; }
        public Builder Write(uint binding, ImageView view, Sampler sampler, uint index = 0) { KoralNative.koral_descriptor_set_builder_write_image_view(Native, null, binding, index, view.Handle, sampler.Handle); return this; }
        public Builder Write(string name, ImageView view, Sampler sampler) { KoralNative.koral_descriptor_set_builder_write_image_view(Native, name, 0, 0, view.Handle, sampler.Handle); return this; }
        public Builder Write(uint binding, Sampler sampler, uint index = 0) { KoralNative.koral_descriptor_set_builder_write_sampler(Native, null, binding, index, sampler.Handle); return this; }
        public Builder Write(string name, Sampler sampler) { KoralNative.koral_descriptor_set_builder_write_sampler(Native, name, 0, 0, sampler.Handle); return this; }
        public Builder Write(uint binding, AccelerationStructure structure, uint index = 0) { KoralNative.koral_descriptor_set_builder_write_acceleration_structure(Native, null, binding, index, structure.Handle); return this; }
        public Builder Write(string name, AccelerationStructure structure) { KoralNative.koral_descriptor_set_builder_write_acceleration_structure(Native, name, 0, 0, structure.Handle); return this; }

        public DescriptorSet Build() => Built<DescriptorSet>(KoralNative.koral_descriptor_set_builder_build(Native));
    }

    public void Rebind(uint binding, Buffer buffer, uint index = 0) => KoralNative.Check(KoralNative.koral_descriptor_set_rebind_buffer(Handle, null, binding, index, buffer.Handle));
    public void Rebind(string name, Buffer buffer) => KoralNative.Check(KoralNative.koral_descriptor_set_rebind_buffer(Handle, name, 0, 0, buffer.Handle));
    public void Rebind(uint binding, Buffer.Slice slice, uint index = 0) => KoralNative.Check(KoralNative.koral_descriptor_set_rebind_buffer_slice(Handle, null, binding, index, slice.Buffer.Handle, slice.Offset, slice.Size));
    public void Rebind(string name, Buffer.Slice slice) => KoralNative.Check(KoralNative.koral_descriptor_set_rebind_buffer_slice(Handle, name, 0, 0, slice.Buffer.Handle, slice.Offset, slice.Size));
    public void Rebind(uint binding, BufferView view, uint index = 0) => KoralNative.Check(KoralNative.koral_descriptor_set_rebind_buffer_view(Handle, null, binding, index, view.Handle));
    public void Rebind(string name, BufferView view) => KoralNative.Check(KoralNative.koral_descriptor_set_rebind_buffer_view(Handle, name, 0, 0, view.Handle));
    public void Rebind(uint binding, ImageView view, uint index = 0) => KoralNative.Check(KoralNative.koral_descriptor_set_rebind_image_view(Handle, null, binding, index, view.Handle, IntPtr.Zero));
    public void Rebind(string name, ImageView view) => KoralNative.Check(KoralNative.koral_descriptor_set_rebind_image_view(Handle, name, 0, 0, view.Handle, IntPtr.Zero));
    public void Rebind(uint binding, ImageView view, Sampler sampler, uint index = 0) => KoralNative.Check(KoralNative.koral_descriptor_set_rebind_image_view(Handle, null, binding, index, view.Handle, sampler.Handle));
    public void Rebind(string name, ImageView view, Sampler sampler) => KoralNative.Check(KoralNative.koral_descriptor_set_rebind_image_view(Handle, name, 0, 0, view.Handle, sampler.Handle));
    public void Rebind(uint binding, Sampler sampler, uint index = 0) => KoralNative.Check(KoralNative.koral_descriptor_set_rebind_sampler(Handle, null, binding, index, sampler.Handle));
    public void Rebind(string name, Sampler sampler) => KoralNative.Check(KoralNative.koral_descriptor_set_rebind_sampler(Handle, name, 0, 0, sampler.Handle));
    public void Rebind(uint binding, AccelerationStructure structure, uint index = 0) => KoralNative.Check(KoralNative.koral_descriptor_set_rebind_acceleration_structure(Handle, null, binding, index, structure.Handle));
    public void Rebind(string name, AccelerationStructure structure) => KoralNative.Check(KoralNative.koral_descriptor_set_rebind_acceleration_structure(Handle, name, 0, 0, structure.Handle));

    public DescriptorSetLayout Layout
    {
        get
        {
            var layout = Wrap<DescriptorSetLayout>(KoralNative.koral_descriptor_set_layout(Handle));
            KoralNative.Check();
            return layout!;
        }
    }
}
