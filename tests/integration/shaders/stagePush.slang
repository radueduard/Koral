// Each stage given what it reads as an entry point's `uniform`s: the vertex shader a matrix, the fragment shader the
// matrix and a colour after it. One block at offset 0, longer in one stage than in the other.

[shader("vertex")]
float4 vertex(uint index : SV_VertexID, uniform float4x4 model) : SV_Position
{
    const float2 corner = float2((index << 1) & 2, index & 2);
    return mul(model, float4(corner * 2.0 - 1.0, 0.0, 1.0));
}

[shader("fragment")]
float4 fragment(uniform float4x4 model, uniform float3 color) : SV_Target
{
    return float4(color, 1.0);
}
