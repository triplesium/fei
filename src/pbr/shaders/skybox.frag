#version 450 core
layout(location = 0) out vec4 Frag_Color;

layout(location = 0) in vec3 Frag_TexCoords;

layout(set = 1, binding = 0) uniform samplerCube skybox;

void main()
{
    Frag_Color = texture(skybox, Frag_TexCoords);
}
