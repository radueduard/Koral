#version 450 core

vec3[3] vertices = vec3[](
    vec3(0.0, 0.5, 0.0),
    vec3(-0.5, -0.5, 0.0),
    vec3(0.5, -0.5, 0.0)
);

vec3[3] colors = vec3[](
    vec3(1.0, 0.0, 0.0),
    vec3(0.0, 1.0, 0.0),
    vec3(0.0, 0.0, 1.0)
);

layout(location = 0) out vec3 fragColor;

layout(binding = 0) uniform Time {
    float time;
};

void main() {
    fragColor = colors[gl_VertexID] * ((sin(time) + 1.f) / 2.f);
    gl_Position = vec4(vertices[gl_VertexID], 1.0);
}