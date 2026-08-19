// The push-constant block, declared once and included by every stage that reads it.
//
// This is what makes a multi-stage push constant work: there is one push-constant range per
// pipeline, so both stages have to place each constant at the same offset — and the surest way to
// place them identically is to write them once. The engine merges the two declarations by name and
// unions their stages, and says so loudly if two stages ever disagree.
//
// The offsets themselves are the compiler's business, and never appear on the CPU side: each
// constant is written by name with CommandBuffer::PushConstant.

layout(push_constant) uniform Push {
    vec2 offset;        // vertex stage only
    vec4 color;         // fragment stage only
} push;
