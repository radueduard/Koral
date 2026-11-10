#version 450
// An image among the debug shapes (kor::DebugDraw): what it holds, tinted. A texel with no alpha is not drawn, so that
// it hides nothing behind it from what is drawn after.

layout(set = 0, binding = 1) uniform sampler2D picture;

layout(location = 0) in vec2 inUv;
layout(location = 1) in vec4 inTint;
layout(location = 0) out vec4 outColor;

void main() {
    outColor = texture(picture, inUv) * inTint;
    if (outColor.a <= 0.0) discard;
}
