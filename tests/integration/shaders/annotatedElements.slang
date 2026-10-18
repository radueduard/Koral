// A storage buffer of a struct whose buffer and fields are annotated: what reflection reads of a StructuredBuffer.

[__AttributeUsage(_AttributeTargets.Var)]
public struct thingAttribute
{
    string semantic;
};

struct Element {
    [thing("WHERE")] float4x4 placed;
    float3 tint;
    uint id;
};
[thing("EACH")] StructuredBuffer<Element> elements;
RWStructuredBuffer<float> output;

[shader("compute")]
[numthreads(1, 1, 1)]
void computeMain(uint3 id : SV_DispatchThreadID)
{
    output[id.x] = elements[id.x].placed[0][0] + elements[id.x].tint.x + float(elements[id.x].id);
}
