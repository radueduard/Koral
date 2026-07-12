// Sample Slang module shipped with the Koral API. A single module exposes multiple entry
// points; the stage of each is taken from its [shader("...")] attribute, so the Koral
// SlangBuilder needs only the module + entry point name:
//
//   Shader::Builder().setLang<Shader::Lang::eSlang>()
//       .setEntryPoint("sample", "vertexMain").getOrBuild();   // stage auto = vertex
//
// Demonstrates cross-module resolution: `import helpers` is resolved across the shader
// search roots (this API folder + any project folder the engine registered).

import helpers;

struct VSOutput
{
    float4 position : SV_Position;
    float3 color    : COLOR;
};

[shader("vertex")]
VSOutput vertexMain(uint vertexId : SV_VertexID)
{
    const float2 positions[3] = { float2(0.0, -0.5), float2(0.5, 0.5), float2(-0.5, 0.5) };

    VSOutput output;
    output.position = float4(positions[vertexId], 0.0, 1.0);
    output.color    = tint(float3(1.0, 0.0, 0.0));
    return output;
}

[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target
{
    return float4(input.color, 1.0);
}
