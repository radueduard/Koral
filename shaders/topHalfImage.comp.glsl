#version 450 core

// Writes the same top-half-green / bottom-half-black pattern as topHalfQuad.vert,
// but straight into a storage image via imageStore. Under Koral's canonical
// top-left image space, row 0 is the top, so the top half (y < H/2) is green.
//
// On the GL backend imageStore is Y-flipped at transpile (see
// injectStorageImageYFlip in backends/open_gl/shader.cpp), so this lands exactly
// where the rasterized quad lands — which is what the orientation parity test
// asserts. Without that flip the two would disagree on GL.
layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;
layout(set = 0, binding = 0, rgba8) uniform writeonly image2D outImage;

void main() {
    const ivec2 coord = ivec2(gl_GlobalInvocationID.xy);
    const ivec2 size  = imageSize(outImage);
    if (coord.x >= size.x || coord.y >= size.y) return;

    const vec4 color = (coord.y < size.y / 2) ? vec4(0.0, 1.0, 0.0, 1.0)
                                              : vec4(0.0, 0.0, 0.0, 1.0);
    imageStore(outImage, coord, color);
}
