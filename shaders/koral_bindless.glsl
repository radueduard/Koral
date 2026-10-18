// koral_bindless.glsl — the bindless tables, for a GLSL shader to reach any resource by the index
// kor::Bindless gave it. The GLSL twin of koral_bindless.slang: same set (kor::BindlessSet, 7), same
// bindings, the same indices under either descriptor model.
//
//     #include "koral_bindless.glsl"
//
//     layout(push_constant) uniform Push { uint albedo; uint linear; };
//     vec4 color = texture(sampler2D(kor_Texture2D[nonuniformEXT(albedo)], kor_Sampler[nonuniformEXT(linear)]), uv);
//
// Declare only what you read beyond these; unread arrays cost nothing. A uniform-buffer table is
// typed by its block, so a shader declares its own at binding 3:
//     layout(set = 7, binding = 3) uniform Material { vec4 albedo; } kor_Materials[];

#ifndef KORAL_BINDLESS_GLSL
#define KORAL_BINDLESS_GLSL

#extension GL_EXT_nonuniform_qualifier : require

// Sampled images (kor::Bindless::Sampled): binding 0.
layout(set = 7, binding = 0) uniform texture2D kor_Texture2D[];
layout(set = 7, binding = 0) uniform texture2DArray kor_Texture2DArray[];
layout(set = 7, binding = 0) uniform textureCube kor_TextureCube[];
layout(set = 7, binding = 0) uniform texture3D kor_Texture3D[];

// Storage images (kor::Bindless::Storage): binding 1.
layout(set = 7, binding = 1, rgba8) uniform image2D kor_Image2D[];

// Storage buffers (kor::Bindless::StorageBuffer): binding 2, as words.
layout(set = 7, binding = 2, std430) buffer KorBuffer { uint words[]; } kor_Buffer[];

// Samplers (kor::Bindless::Sampler): binding 4.
layout(set = 7, binding = 4) uniform sampler kor_Sampler[];

#endif
