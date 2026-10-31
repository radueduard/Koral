// Two buffers whose structs have a member of one name, annotated differently, and one not at all: what each means is
// its own struct's. Exercised by AStructuredBuffersElementsAreAnnotatedAsTheirOwnStruct.

[__AttributeUsage(_AttributeTargets.Var)]
struct thingAttribute {
    string semantic;
};
[__AttributeUsage(_AttributeTargets.Var)]
struct otherAttribute {
    string semantic;
};

struct First {
    [thing("FIRST")] float4 shared;
    [thing("ONLY_FIRST")] float4 plain;
};

struct Second {
    [other("SECOND")] float4 shared;
    float4 plain;
};

StructuredBuffer<First> firsts;
StructuredBuffer<Second> seconds;
RWStructuredBuffer<float> output;

[shader("compute")]
[numthreads(1, 1, 1)]
void computeMain()
{
    output[0] = firsts[0].shared.x + firsts[0].plain.x + seconds[0].shared.x + seconds[0].plain.x;
}
