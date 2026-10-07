#version 460
// A quad drawn once an instance, each placed by a transform of its own: a mat4 per instance, which takes four
// locations (one a column), then a colour after it. Used by the tests of multi-location vertex inputs.
layout(location = 0) in vec2 position;
layout(location = 1) in mat4 model;
layout(location = 5) in vec4 color;

layout(location = 0) out vec4 outColor;

void main()
{
    gl_Position = model * vec4(position, 0.0, 1.0);
    outColor = color;
}
