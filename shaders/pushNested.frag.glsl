#version 450 core

// A push-constant block with everything awkward in it: a matrix, a nested struct, and an array.
// What it pins is how far the by-name lookup reaches — reflection reports the block's *top-level*
// members, so `material` is one 20-byte constant and `material.albedo` is not a name at all.
struct Material {
    vec4 albedo;
    float roughness;
};

layout(push_constant) uniform Push {
    mat4 model;             // pushes the struct off offset 0, so its offset is worth checking
    Material material;
    float weights[4];
} push;

layout(location = 0) out vec4 outColor;

void main() {
    // Reads across the nested struct and the array, so a write that landed at the wrong offset
    // shows up in the readback rather than being optimised away.
    outColor = vec4(push.material.albedo.rgb * push.material.roughness, push.weights[2]);
}
