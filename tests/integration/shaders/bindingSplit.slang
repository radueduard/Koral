// A compute shader that says nothing of where its descriptors go: whoever builds a pipeline from it
// numbers them (Pipeline SetBinding). Two specialization constants and a push-constant block besides.

[vk::constant_id(3)] const int scale = 2;
[vk::constant_id(7)] const float offset = 0.5;

struct Params {
    uint count;
    float bias;
};
[[vk::push_constant]] ConstantBuffer<Params> params;

StructuredBuffer<float> input;
RWStructuredBuffer<float> output;

[shader("compute")]
[numthreads(64, 1, 1)]
void computeMain(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= params.count) return;
    output[id.x] = input[id.x] * scale + offset + params.bias;
}
