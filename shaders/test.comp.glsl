#version 450 core
#extension GL_NV_uniform_buffer_std430_layout : enable

layout (local_size_x = 16, local_size_y = 16, local_size_z = 1) in;


layout (binding = 0, std430) uniform Square {
    vec3 position;
    vec4 color;
    float size;
} square;

layout (binding = 1, rgba8) uniform writeonly image2D img_output;

void main() {
    ivec2 pixel_coords = ivec2(gl_GlobalInvocationID.xy);
    vec2 uv = (vec2(pixel_coords) + 0.5) / vec2(imageSize(img_output));

    vec2 bottom_left = square.position.xy - square.size * 0.5;
    vec2 top_right = square.position.xy + square.size * 0.5;

    bool inside = all(greaterThanEqual(uv, bottom_left)) && all(lessThanEqual(uv, top_right));
    vec4 output_color = inside ? square.color : vec4(0.0, 0.0, 0.0, 1.0);

    imageStore(img_output, pixel_coords, output_color);
}