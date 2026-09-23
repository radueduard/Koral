// Projects an equirectangular (lat-long) panorama onto the six faces of a cubemap.
//
// One thread per texel of one face; the cube is written as a 2D array of six layers, in the
// standard face order — +X, -X, +Y, -Y, +Z, -Z — which is the layer order a cube view expects.
//
// The direction each texel looks along is the cube convention Vulkan and D3D share: face-local
// coordinates run left-to-right and top-to-bottom, which is also Koral's canonical image space, so
// a face written here reads the same way a face loaded from a file does. The panorama is sampled
// with longitude around Y and latitude from +Y down, so row 0 of the source is the sky.
//
// Shipped by the image module (koral.image) and compiled by it on first use; there is no reason
// for a project to name this file.

[[vk::binding(0, 0)]] Texture2D<float4> equirect;
[[vk::binding(1, 0)]] SamplerState equirectSampler;
// The written format is declared rather than left to the pipeline, so this needs no
// shaderStorageImageWriteWithoutFormat: the cube is always 32-bit float, which is also what keeps
// an HDR panorama's range intact on the way through.
[format("rgba32f")]
[[vk::binding(2, 0)]] RWTexture2DArray<float4> cube;

static const float kPi = 3.14159265358979323846f;

// The direction a texel of `face` at face-local (u, v) in [-1, 1] looks along.
float3 directionFor(uint face, float u, float v)
{
    switch (face)
    {
        case 0: return float3( 1.0f,   -v,   -u);   // +X  right
        case 1: return float3(-1.0f,   -v,    u);   // -X  left
        case 2: return float3(    u, 1.0f,    v);   // +Y  top
        case 3: return float3(    u,-1.0f,   -v);   // -Y  bottom
        case 4: return float3(    u,   -v, 1.0f);   // +Z  front
        default:return float3(   -u,   -v,-1.0f);   // -Z  back
    }
}

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint width, height, faces;
    cube.GetDimensions(width, height, faces);
    if (id.x >= width || id.y >= height || id.z >= faces) return;

    // Texel centres, so the four texels around a face corner straddle the seam evenly.
    const float u = (float(id.x) + 0.5f) / float(width)  * 2.0f - 1.0f;
    const float v = (float(id.y) + 0.5f) / float(height) * 2.0f - 1.0f;

    const float3 direction = normalize(directionFor(id.z, u, v));

    // Longitude from atan2 covers the full circle; latitude from acos puts +Y at row 0.
    const float longitude = atan2(direction.z, direction.x);
    const float latitude  = acos(clamp(direction.y, -1.0f, 1.0f));

    const float2 uv = float2(longitude / (2.0f * kPi) + 0.5f, latitude / kPi);

    cube[id] = equirect.SampleLevel(equirectSampler, uv, 0.0f);
}
