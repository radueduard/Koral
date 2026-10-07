#version 460
#extension GL_EXT_shader_atomic_float : require
// Every invocation adds 1.0 to one float: needs kor::Feature::eAtomicFloat32 enabled on the device.
layout (local_size_x = 64) in;
layout (set = 0, binding = 0) buffer Sum { float value; } sum;
void main() { atomicAdd(sum.value, 1.0); }
