#version 460
// Reaches its data through a raw 64-bit pointer rather than a bound descriptor.
// Reflection can see that the dereference happens (the PhysicalStorageBufferAddresses
// capability) but not which buffer it lands on, so the engine cannot synchronise it —
// it reports the hazard instead. Used by the device-address diagnostic test.
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

layout(local_size_x = 64) in;

layout(buffer_reference, std430) buffer Values {
    uint v[];
};

layout(push_constant) uniform Push {
    uint64_t address;
} push;

void main() {
    Values values = Values(push.address);
    values.v[gl_GlobalInvocationID.x] *= 2u;
}
