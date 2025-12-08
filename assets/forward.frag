#version 450 core
in vec2 Frag_TexCoords;
out vec4 Out_Color;

layout(binding = 0, row_major, std140) uniform Material {
    vec3 base_color;
};
layout(binding = 1) uniform sampler2D image;

void main()
{
    Out_Color = texture(image, Frag_TexCoords) * vec4(base_color, 1.0);
}
