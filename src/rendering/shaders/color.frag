#version 450 core
in vec2 Frag_TexCoords;
out vec4 Out_Color;

layout(row_major, std140) uniform Material {
    vec3 base_color;
};

void main()
{
    Out_Color = vec4(base_color, 1.0);
}
