// A push-constant block that, with a set beside it, leaves no room in the descriptor heap's 256 bytes of
// push data for where the set's descriptors start. Fine under descriptor sets.

struct Wide { float4 values[16]; };
[[vk::push_constant]] ConstantBuffer<Wide> wide;
[[vk::binding(0, 1)]] RWStructuredBuffer<float4> wideOutput;

[shader("compute")]
[numthreads(1, 1, 1)]
void widePush(uint3 id : SV_DispatchThreadID)
{
    wideOutput[0] = wide.values[15];
}
