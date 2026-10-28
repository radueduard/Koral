// Push constants declared with defaults and attributes, as a tool that shows them to be set reads them: each of an
// entry point's uniforms, and a struct's fields.

[__AttributeUsage(_AttributeTargets.Param)]
struct RangeAttribute { float min; float max; };
[__AttributeUsage(_AttributeTargets.Param)]
struct ColorAttribute {};
[__AttributeUsage(_AttributeTargets.Var)]
struct StepsAttribute { int each; };

struct Wave
{
    [Steps(4)] float length = 2.5;      // a struct's field: its default and its attribute
    float3 direction = float3(1, 0, 0.5);
    float2 offset = float2(3);          // one number, for each component
    float worked = 2.0 * 3.0;           // not written as numbers: there, and not read
};

RWStructuredBuffer<float> output;

[shader("compute")]
[numthreads(1, 1, 1)]
void computeMain(uint3 id : SV_DispatchThreadID, uniform Wave wave, uniform float plain,
                 [Range(0, 50)] uniform float speed = 10.0,
                 [Color] uniform float4 tint = float4(1, 0.5, 0, 1),
                 uniform int count = -3)
{
    output[0] = speed + tint.x + count + plain + wave.length + wave.direction.z + wave.offset.y + wave.worked;
}
