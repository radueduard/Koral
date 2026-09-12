#version 450 core

// Integration-test compute shader for kor::BufferView: reads a texel buffer through a
// samplerBuffer and writes the first channel of each texel into a storage buffer.
//
// The point of the source binding is that nothing here declares what the bytes are. The view
// carries the format (eRGBA32_SFLOAT), so texelFetch returns a vec4 without a struct saying so —
// which is the whole difference between this and binding the buffer as a storage buffer.
//
// One invocation per texel; dispatched with exactly ceil(count / 64) work groups, so no bounds
// check is needed for multiples of 64.
layout (local_size_x = 64, local_size_y = 1, local_size_z = 1) in;

layout (set = 0, binding = 0) uniform samplerBuffer source;

layout (set = 0, binding = 1) buffer Destination {
    float values[];
} destination;

void main() {
    const uint i = gl_GlobalInvocationID.x;
    destination.values[i] = texelFetch(source, int(i)).x;
}
