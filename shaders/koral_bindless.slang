// koral_bindless — the bindless tables, for a shader to reach any resource by the index kor::Bindless
// gave it.
//
//     import koral_bindless;
//
//     [[vk::push_constant]] cbuffer Push { uint albedo; uint linear; uint vertices; };
//     float4 color = kor::texture2D(albedo).Sample(kor::sampler(linear), uv);
//     float3 p = kor::buffer(vertices).Load<float3>(vertexId * 12);
//
// Or typed, with Slang's descriptor handles — the handle is used as the resource it names, and the engine
// knows from its type which table to hand out an index into:
//
//     struct Push { RWTexture2D<float4>.Handle output; Texture2D.Handle albedo; SamplerState.Handle linear; };
//     push.output[id] = push.albedo.SampleLevel(push.linear, uv, 0);
//     cb.PushConstant("push.output", image);       // C++: any resource, or a kor::DescriptorIndex
//
// A `ConstantBuffer<T>.Handle` is reached as `(*handle).field`.
//
// The same SPIR-V runs under either descriptor model: under descriptor sets these arrays are one set
// the engine binds for you; under the descriptor heap they are the heap. They sit at kor::BindlessSet
// (7) — keep a pipeline's own sets below it — one table per kind of descriptor, several array types
// aliasing one table where they describe the same kind (a sampled image is a sampled image whatever
// its shape). Only what a shader actually reads ends up in it, so importing this costs nothing.
//
// Indices are non-uniform: each invocation may pick a different resource.

module koral_bindless;

// Sampled images (kor::Bindless::Sampled): binding 0.
[[vk::binding(0, 7)]] public Texture1D<float4> kor_Texture1D[];
[[vk::binding(0, 7)]] public Texture2D<float4> kor_Texture2D[];
[[vk::binding(0, 7)]] public Texture2DArray<float4> kor_Texture2DArray[];
[[vk::binding(0, 7)]] public TextureCube<float4> kor_TextureCube[];
[[vk::binding(0, 7)]] public TextureCubeArray<float4> kor_TextureCubeArray[];
[[vk::binding(0, 7)]] public Texture3D<float4> kor_Texture3D[];
[[vk::binding(0, 7)]] public Texture2D<uint4> kor_UTexture2D[];

// Storage images (kor::Bindless::Storage): binding 1.
[[vk::binding(1, 7)]] public RWTexture2D<float4> kor_RWTexture2D[];
[[vk::binding(1, 7)]] public RWTexture2DArray<float4> kor_RWTexture2DArray[];
[[vk::binding(1, 7)]] public RWTexture3D<float4> kor_RWTexture3D[];
[[vk::binding(1, 7)]] public RWTexture2D<uint4> kor_RWUTexture2D[];

// Storage buffers (kor::Bindless::StorageBuffer): binding 2, as raw bytes.
[[vk::binding(2, 7)]] public ByteAddressBuffer kor_Buffer[];
[[vk::binding(2, 7)]] public RWByteAddressBuffer kor_RWBuffer[];

// Uniform buffers (kor::Bindless::UniformBuffer) are binding 3, and typed by their block, so a shader
// declares its own table there:
//     [[vk::binding(3, 7)]] ConstantBuffer<Material> materials[];

// Samplers (kor::Bindless::Sampler): binding 4.
[[vk::binding(4, 7)]] public SamplerState kor_Sampler[];
[[vk::binding(4, 7)]] public SamplerComparisonState kor_ComparisonSampler[];

// Top-level acceleration structures (kor::Bindless::AccelerationStructure): binding 5.
[[vk::binding(5, 7)]] public RaytracingAccelerationStructure kor_AccelerationStructure[];

namespace kor
{
    public Texture1D<float4> texture1D(uint index) { return kor_Texture1D[NonUniformResourceIndex(index)]; }
    public Texture2D<float4> texture2D(uint index) { return kor_Texture2D[NonUniformResourceIndex(index)]; }
    public Texture2DArray<float4> texture2DArray(uint index) { return kor_Texture2DArray[NonUniformResourceIndex(index)]; }
    public TextureCube<float4> textureCube(uint index) { return kor_TextureCube[NonUniformResourceIndex(index)]; }
    public TextureCubeArray<float4> textureCubeArray(uint index) { return kor_TextureCubeArray[NonUniformResourceIndex(index)]; }
    public Texture3D<float4> texture3D(uint index) { return kor_Texture3D[NonUniformResourceIndex(index)]; }
    public Texture2D<uint4> utexture2D(uint index) { return kor_UTexture2D[NonUniformResourceIndex(index)]; }

    public RWTexture2D<float4> image2D(uint index) { return kor_RWTexture2D[NonUniformResourceIndex(index)]; }
    public RWTexture2DArray<float4> image2DArray(uint index) { return kor_RWTexture2DArray[NonUniformResourceIndex(index)]; }
    public RWTexture3D<float4> image3D(uint index) { return kor_RWTexture3D[NonUniformResourceIndex(index)]; }
    public RWTexture2D<uint4> uimage2D(uint index) { return kor_RWUTexture2D[NonUniformResourceIndex(index)]; }

    public ByteAddressBuffer buffer(uint index) { return kor_Buffer[NonUniformResourceIndex(index)]; }
    public RWByteAddressBuffer rwBuffer(uint index) { return kor_RWBuffer[NonUniformResourceIndex(index)]; }

    public SamplerState sampler(uint index) { return kor_Sampler[NonUniformResourceIndex(index)]; }
    public SamplerComparisonState comparisonSampler(uint index) { return kor_ComparisonSampler[NonUniformResourceIndex(index)]; }

    public RaytracingAccelerationStructure accelerationStructure(uint index) { return kor_AccelerationStructure[NonUniformResourceIndex(index)]; }
}

// Where a `T.Handle` leads: Slang turns every use of a descriptor handle into a call to this, and without it
// would index arrays of its own at set 0 — the pipeline's. These alias the tables above, binding for binding,
// typed by each use; the index is the handle's first word, as kor::Bindless hands it out.
[[vk::binding(0, 7)]] __DynamicResource<__DynamicResourceKind.General> kor_SampledTable[];
[[vk::binding(1, 7)]] __DynamicResource<__DynamicResourceKind.General> kor_StorageImageTable[];
[[vk::binding(2, 7)]] __DynamicResource<__DynamicResourceKind.General> kor_StorageBufferTable[];
[[vk::binding(3, 7)]] __DynamicResource<__DynamicResourceKind.General> kor_UniformBufferTable[];
[[vk::binding(4, 7)]] __DynamicResource<__DynamicResourceKind.Sampler> kor_SamplerTable[];
[[vk::binding(5, 7)]] __DynamicResource<__DynamicResourceKind.General> kor_AccelerationStructureTable[];

public export T getDescriptorFromHandle<T : IOpaqueDescriptor>(DescriptorHandle<T> handle)
{
    const uint index = NonUniformResourceIndex(((uint2)handle).x);
    switch (T.kind)
    {
    case DescriptorKind.Sampler:
        return kor_SamplerTable[index].asOpaqueDescriptor<T>();
    case DescriptorKind.Texture:
        if (T.descriptorAccess == DescriptorAccess.Read) return kor_SampledTable[index].asOpaqueDescriptor<T>();
        return kor_StorageImageTable[index].asOpaqueDescriptor<T>();
    case DescriptorKind.ConstantBuffer:
        return kor_UniformBufferTable[index].asOpaqueDescriptor<T>();
    case DescriptorKind.StorageBuffer:
        return kor_StorageBufferTable[index].asOpaqueDescriptor<T>();
    case DescriptorKind.AccelerationStructure:
        return kor_AccelerationStructureTable[index].asOpaqueDescriptor<T>();
    default:
        // Texel buffers and combined image-samplers have no table of their own: the engine refuses to push
        // anything into such a handle, so this is never reached with a real index.
        return kor_SampledTable[index].asOpaqueDescriptor<T>();
    }
}
