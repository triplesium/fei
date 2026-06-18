#version 450 core

layout(location = 0) in vec2 Frag_TexCoords;
layout(location = 0) out vec4 Out_Color;

layout(set = 2, binding = 0, row_major, std140) uniform Material {
    vec3 albedo;
    float metallic;
    float roughness;
    int flags;
} material;

void main()
{
    Out_Color = vec4(material.albedo, 1.0);
}
