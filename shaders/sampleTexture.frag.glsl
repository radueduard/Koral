#version 450 core

// Samples a combined image/sampler descriptor at set 0, binding 0. Paired with
// sampleTexture.vert to verify the descriptor-set image+sampler bind path
// end-to-end. With a solid-color source texture the readback is independent of
// UV orientation (GL bottom-left vs Vulkan top-left).
#extension GL_KHR_vulkan_glsl : enable

layout(location = 0) in vec2 uv;
layout(set = 0, binding = 0) uniform sampler2D tex;
layout(location = 0) out vec4 outColor;

void main() {
    outColor = texture(tex, uv);
}
