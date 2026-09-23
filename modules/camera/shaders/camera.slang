// The camera module's annotation, and nothing else.
//
//     import camera;
//
//     struct Frame {
//         [camera("VIEW_PROJECTION_MATRIX")] float4x4 viewProjection;
//         [camera("POSITION")]               float3   eye;
//         float4x4 mine;                     // not annotated: never written, yours to fill
//     };
//     ParameterBlock<Frame> frame;
//
// The attribute is named after the module, which is what makes `camera(SEMANTIC)` mean "the camera
// module's SEMANTIC" — Slang takes the attribute name from the struct below, minus the trailing
// "Attribute". GLSL says the same thing with `#pragma camera(VIEW_PROJECTION_MATRIX)` on the line
// above the field, and needs nothing imported.
//
// An attribute rather than `: SEMANTIC`, because Slang drops semantics on anything that is not a
// varying — reflection would have nothing to read. @see koralCamera.h for the vocabulary.

module camera;

[__AttributeUsage(_AttributeTargets.Var)]
public struct cameraAttribute
{
    string semantic;
};
