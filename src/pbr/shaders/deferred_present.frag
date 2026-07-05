#version 450 core

layout(location = 0) out vec4 fragColor;

layout(location = 0) in vec2 Frag_TexCoords;

layout(set = 0, binding = 0) uniform sampler2D composite;

void main()
{
    fragColor = texture(composite, Frag_TexCoords);
}
