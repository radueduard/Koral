// Slang descriptor handles (`T.Handle`) as push constants: koral_bindless resolves them into the bindless
// tables, and the engine reads from their types which table to hand an index out of.

import koral_bindless;

struct Images {
    RWTexture2D<float4>.Handle target;
    Texture2D.Handle texture;
    SamplerState.Handle sampler;
};

// Copies a sampled texel into every texel of a storage image — all three reached by handle, from an entry
// point's `uniform` parameter (pushed as "push.target" and so on).
[shader("compute")]
[numthreads(2, 2, 1)]
void images(uint3 id : SV_DispatchThreadID, uniform Images push)
{
    push.target[id.xy] = push.texture.SampleLevel(push.sampler, float2(0.5, 0.5), 0);
}

struct Buffers {
    StructuredBuffer<float>.Handle source;
    RWStructuredBuffer<float4>.Handle output;
    uint count;
};
[[vk::push_constant]] ConstantBuffer<Buffers> handles;

// Doubles `count` floats, buffers reached by handle from a push-constant block (pushed as "source").
[shader("compute")]
[numthreads(64, 1, 1)]
void buffers(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= handles.count) return;
    handles.output[id.x] = float4(handles.source[id.x] * 2, 0, 0, 0);
}
