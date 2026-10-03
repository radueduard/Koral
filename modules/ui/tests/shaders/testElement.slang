// The Slang twin of testElement.frag.glsl: the left half one parameter colour, the right half the other.
import koralUI;

struct Params { float4 left; float4 right; };
[[vk::binding(0, 1)]] StructuredBuffer<Params> parameters;

[shader("fragment")]
float4 fragmentMain(KuiVaryings v) : SV_Target {
    KuiFragment f = kuiFragment(v);
    Params p = parameters[f.parameters];
    return kuiFinish(f, f.uv.x < 0.5 ? p.left : p.right);
}
