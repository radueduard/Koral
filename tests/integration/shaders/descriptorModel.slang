// What the descriptor-model tests run: each entry point reaches its resources a different way — by
// bindless index, through an embedded sampler, through a set the pipeline numbers itself — and writes
// what it found to `output`, for the test to read back. Every one of them runs under both models.

import koral_bindless;

struct Indices {
    uint source;    // a storage buffer's bindless index
    uint texture;   // a sampled image's
    uint sampler;   // a sampler's
    uint count;
};
[[vk::push_constant]] ConstantBuffer<Indices> push;

[[vk::binding(0, 0)]] RWStructuredBuffer<float4> output;

// Doubles `count` floats read through a bindless storage buffer.
[shader("compute")]
[numthreads(64, 1, 1)]
void bindlessBuffer(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= push.count) return;
    const float value = asfloat(kor::buffer(push.source).Load(id.x * 4));
    output[id.x] = float4(value * 2, 0, 0, 0);
}

// Samples a bindless texture through a bindless sampler.
[shader("compute")]
[numthreads(1, 1, 1)]
void bindlessTexture(uint3 id : SV_DispatchThreadID)
{
    output[0] = kor::texture2D(push.texture).SampleLevel(kor::sampler(push.sampler), float2(0.5, 0.5), 0);
}

// The sampler at binding 2 and the one in `combined` are the pipeline's (SetSampler): nothing is written for them.
[[vk::binding(1, 0)]] Texture2D<float4> image;
[[vk::binding(2, 0)]] SamplerState embedded;
[[vk::binding(3, 0)]] Sampler2D<float4> combined;

[shader("compute")]
[numthreads(1, 1, 1)]
void embeddedSamplers(uint3 id : SV_DispatchThreadID)
{
    output[0] = image.SampleLevel(embedded, float2(0.5, 0.5), 0);
    output[1] = combined.SampleLevel(float2(0.5, 0.5), 0);
}

// Writes the colour into every texel of a bindless storage image.
[shader("compute")]
[numthreads(2, 2, 1)]
void bindlessImage(uint3 id : SV_DispatchThreadID)
{
    kor::image2D(push.texture)[id.xy] = float4(0.25, 0.5, 0.75, 1.0);
}
