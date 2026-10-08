#version 450
// GLSL with no layout(binding): numbered by glslang, and by any pipeline built from it.
layout(local_size_x = 64) in;
layout(constant_id = 0) const uint factor = 3;
layout(std430) readonly buffer Source { float values[]; } source;
layout(std430) writeonly buffer Target { float values[]; } target;
void main() { target.values[gl_GlobalInvocationID.x] = source.values[gl_GlobalInvocationID.x] * float(factor); }
