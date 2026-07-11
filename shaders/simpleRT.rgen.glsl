#version 460
#extension GL_EXT_ray_tracing : require

// Minimal ray generation shader for the ray-tracing coverage test: one ray per
// pixel shot straight down +Z through the scene, writing the payload (green on
// hit, blue on miss) into a storage image.
layout(binding = 0, set = 0) uniform accelerationStructureEXT topLevelAS;
layout(binding = 1, set = 0, rgba8) uniform image2D outImage;

layout(location = 0) rayPayloadEXT vec3 hitValue;

void main() {
    const vec2 uv = (vec2(gl_LaunchIDEXT.xy) + 0.5) / vec2(gl_LaunchSizeEXT.xy);
    const vec3 origin = vec3(uv * 2.0 - 1.0, -1.0);
    const vec3 direction = vec3(0.0, 0.0, 1.0);

    hitValue = vec3(0.0);
    traceRayEXT(topLevelAS, gl_RayFlagsOpaqueEXT, 0xFF, 0, 0, 0, origin, 0.001, direction, 100.0, 0);
    imageStore(outImage, ivec2(gl_LaunchIDEXT.xy), vec4(hitValue, 1.0));
}
