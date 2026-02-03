#version 450 core
in vec2 Frag_TexCoords;
out vec4 Out_Color;

layout(row_major, std140) uniform Material {
    vec3 albedo;
    float metallic;
    float roughness;
    int flags;
} material;

void main()
{
    Out_Color = vec4(material.albedo, 1.0);
}
