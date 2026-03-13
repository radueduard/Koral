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

struct Light {
    vec4 position;
    vec4 color;
};

layout (set = 0, binding = 7, std430) buffer Lights {
    Light light[];
};

void main() {
    vec2 uv = vec2(gl_GlobalInvocationID.xy) / vec2(imageSize(outputImage));

    vec3 position = texture(gPosition, uv).rgb;
    vec3 albedo = texture(gAlbedo, uv).rgb;
    vec3 normal = normalize(texture(gNormal, uv).rgb * 2.0 - 1.0);
    vec3 metallicRoughnessAO = texture(gMetallicRoughnessAO, uv).rgb;
    vec3 emission = texture(gEmission, uv).rgb;

    vec3 diffuse = vec3(0.0);
    for (int i = 0; i < light.length(); ++i) {
        vec3 lightDir = normalize(light[i].position.xyz - position);
        float diff = max(dot(normal, lightDir), 0.0);
        diffuse += diff * light[i].color.rgb * albedo;
    }

    // Combine with emission
    vec3 finalColor = diffuse + emission;

    imageStore(outputImage, ivec2(gl_GlobalInvocationID.xy), vec4(finalColor, 1.0));
}