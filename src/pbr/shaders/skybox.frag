#version 450 core
out vec4 Frag_Color;

in vec3 Frag_TexCoords;

uniform samplerCube skybox;

void main()
{
    Frag_Color = texture(skybox, Frag_TexCoords);
}
