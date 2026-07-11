#version 450 core

// Integration-test compute shader: doubles every uint in a storage buffer,
// in place. One invocation per element; dispatched with exactly
// ceil(count / 64) work groups so no bounds check is needed for multiples of 64.
layout (local_size_x = 64, local_size_y = 1, local_size_z = 1) in;

layout (set = 0, binding = 0) buffer Data {
    uint values[];
} data;

void main() {
    const uint i = gl_GlobalInvocationID.x;
    data.values[i] = data.values[i] * 2u;
}
