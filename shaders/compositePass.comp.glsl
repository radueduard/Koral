#version 450 core
layout (local_size_x = 16, local_size_y = 16) in;

layout (set = 0, binding = 0) uniform sampler2D gPosition;
layout (set = 0, binding = 1) uniform sampler2D gAlbedo;
layout (set = 0, binding = 2) uniform sampler2D gNormal;
layout (set = 0, binding = 3) uniform sampler2D gMetallicRoughnessAO;
layout (set = 0, binding = 4) uniform sampler2D gEmission;

layout (set = 0, binding = 5) uniform writeonly image2D outputImage;

layout (set = 0, binding = 6) uniform Camera {
    mat4 view;
    mat4 projection;
} camera;

layout (set = 0, binding = 7) buffer Light {
    vec3 position;
    vec3 color;
} light[];

void main() {
    ivec2 pixelCoords = ivec2(gl_GlobalInvocationID.xy);
    vec3 position = texelFetch(gPosition, pixelCoords, 0).rgb;
    vec3 albedo = texelFetch(gAlbedo, pixelCoords, 0).rgb;
    vec3 normal = texelFetch(gNormal, pixelCoords, 0).rgb;
    vec3 metallicRoughnessAO = texelFetch(gMetallicRoughnessAO, pixelCoords, 0).rgb;
    vec3 emission = texelFetch(gEmission, pixelCoords, 0).rgb;

    // Simple lighting calculation (for demonstration)
    vec3 lightDir = normalize(light[0].position - position);
    float diff = max(dot(normal, lightDir), 0.0);
    vec3 diffuse = diff * light[0].color * albedo;

    // Combine with emission
    vec3 finalColor = diffuse + emission;

    imageStore(outputImage, pixelCoords, vec4(finalColor, 1.0));
}