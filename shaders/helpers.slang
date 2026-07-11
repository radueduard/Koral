// Shared helper module shipped with the gfx API. Projects can `import helpers;` because
// the API's shaders/ folder is always a shader search root.

public float3 tint(float3 c)
{
    return c * 0.9 + 0.05;
}
