#version 450 core
layout (location = 0) out vec4 g_position_ao;
layout (location = 1) out vec4 g_normal_roughness;
layout (location = 2) out vec4 g_albedo_metallic;
layout (location = 3) out vec4 g_specular;
layout (location = 4) out vec4 g_emissive_depth;

in vec3 Frag_Position;
in vec3 Frag_Normal;
in vec2 Frag_TexCoords;
in vec3 Frag_Tangent;

uniform sampler2D albedo_map;
uniform sampler2D normal_map;
uniform sampler2D metallic_map;
uniform sampler2D roughness_map;
uniform sampler2D emissive_map;
uniform sampler2D specular_map;

const int STANDARD_MATERIAL_FLAGS_ALBEDO_MAP_BIT = 1 << 0;
const int STANDARD_MATERIAL_FLAGS_NORMAL_MAP_BIT = 1 << 1;
const int STANDARD_MATERIAL_FLAGS_METALLIC_MAP_BIT = 1 << 2;
const int STANDARD_MATERIAL_FLAGS_ROUGHNESS_MAP_BIT = 1 << 3;
const int STANDARD_MATERIAL_FLAGS_EMISSIVE_MAP_BIT = 1 << 4;
const int STANDARD_MATERIAL_FLAGS_SPECULAR_MAP_BIT = 1 << 5;

layout(row_major, std140) uniform View {
    mat4 clip_from_world;
    mat4 view_from_world;
    mat4 clip_from_view;
    vec3 world_position;
} view;

layout(row_major, std140) uniform Mesh {
    mat4 world_from_local;
} mesh;

layout(row_major, std140) uniform Material {
    vec3 albedo;
    float metallic;
    float roughness;
    vec3 emissive;
    vec3 specular;
    int flags;
} material;

vec3 normal_mapping() {
    vec3 tangentNormal = texture(normal_map, Frag_TexCoords).xyz * 2.0 - 1.0;
    vec3 N = normalize(Frag_Normal);
    vec3 T = normalize(Frag_Tangent);
    T = normalize(T - dot(T, N) * N);
    vec3 B = normalize(cross(N, T));
    mat3 TBN = mat3(T, B, N);
    return normalize(TBN * tangentNormal);
}

void main()
{    
    g_position_ao.rgb = Frag_Position;
    g_position_ao.a = 1.0;
    g_normal_roughness.rgb = (material.flags & STANDARD_MATERIAL_FLAGS_NORMAL_MAP_BIT) != 0 ? normal_mapping() : normalize(Frag_Normal);
    g_normal_roughness.a = (material.flags & STANDARD_MATERIAL_FLAGS_ROUGHNESS_MAP_BIT) != 0 ? texture(roughness_map, Frag_TexCoords).r : material.roughness;
    g_normal_roughness.a = clamp(g_normal_roughness.a, 0.05, 0.96);
    g_albedo_metallic.rgb = (material.flags & STANDARD_MATERIAL_FLAGS_ALBEDO_MAP_BIT) != 0 ? texture(albedo_map, Frag_TexCoords).rgb : material.albedo;
    g_albedo_metallic.a = (material.flags & STANDARD_MATERIAL_FLAGS_METALLIC_MAP_BIT) != 0 ? texture(metallic_map, Frag_TexCoords).r : material.metallic;
    g_specular.rgb = (material.flags & STANDARD_MATERIAL_FLAGS_SPECULAR_MAP_BIT) != 0 ? texture(specular_map, Frag_TexCoords).rgb : material.specular;
    g_specular.a = 1.0;
    g_emissive_depth.rgb = (material.flags & STANDARD_MATERIAL_FLAGS_EMISSIVE_MAP_BIT) != 0 ? texture(emissive_map, Frag_TexCoords).rgb : material.emissive;
    g_emissive_depth.a = gl_FragCoord.z;
}  
