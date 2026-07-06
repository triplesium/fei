#include "mesh_common.slangh"

struct VertexInput {
    [vk::location(0)] float3 position : POSITION;
    [vk::location(1)] float3 normal : NORMAL;
    [vk::location(2)] float2 uv : TEXCOORD0;
    [vk::location(4)] float3 tangent : TANGENT;
};

struct VertexOutput {
    float4 position : SV_Position;
    [vk::location(0)] float3 position_ws : POSITION_WS;
    [vk::location(1)] float3 normal_ws : NORMAL_WS;
    [vk::location(2)] float2 tex_coords : TEXCOORD0;
    [vk::location(3)] float3 tangent_ws : TANGENT_WS;
};

struct FragmentOutput {
    [vk::location(0)] float4 position_ao : SV_Target0;
    [vk::location(1)] float4 normal_roughness : SV_Target1;
    [vk::location(2)] float4 albedo_metallic : SV_Target2;
    [vk::location(3)] float4 specular : SV_Target3;
    [vk::location(4)] float4 emissive_depth : SV_Target4;
};

layout(set = 0, binding = 0) ConstantBuffer<ViewData> View;
layout(set = 1, binding = 0) ConstantBuffer<MeshData> Mesh;
layout(set = 2, binding = 0) ConstantBuffer<PbrMaterialData> Material;
layout(set = 2, binding = 1) Texture2D<float4> albedo_map;
layout(set = 2, binding = 2) Texture2D<float4> normal_map;
layout(set = 2, binding = 3) Texture2D<float4> metallic_map;
layout(set = 2, binding = 4) Texture2D<float4> roughness_map;
layout(set = 2, binding = 5) Texture2D<float4> emissive_map;
layout(set = 2, binding = 6) Texture2D<float4> specular_map;
layout(set = 2, binding = 7) SamplerState sampler;

static const int STANDARD_MATERIAL_FLAGS_ALBEDO_MAP_BIT = 1 << 0;
static const int STANDARD_MATERIAL_FLAGS_NORMAL_MAP_BIT = 1 << 1;
static const int STANDARD_MATERIAL_FLAGS_METALLIC_MAP_BIT = 1 << 2;
static const int STANDARD_MATERIAL_FLAGS_ROUGHNESS_MAP_BIT = 1 << 3;
static const int STANDARD_MATERIAL_FLAGS_EMISSIVE_MAP_BIT = 1 << 4;
static const int STANDARD_MATERIAL_FLAGS_SPECULAR_MAP_BIT = 1 << 5;

float3 normal_mapping(VertexOutput input) {
    float3 tangent_normal =
        normal_map.Sample(sampler, input.tex_coords).xyz * 2.0 - 1.0;
    float3 normal = normalize(input.normal_ws);
    float3 tangent = normalize(input.tangent_ws);
    tangent = normalize(tangent - dot(tangent, normal) * normal);
    float3 bitangent = normalize(cross(normal, tangent));
    float3x3 tbn = float3x3(tangent, bitangent, normal);
    return normalize(mul(tangent_normal, tbn));
}

[shader("vertex")]
VertexOutput vertex_main(VertexInput input) {
    VertexOutput output;
    output.position_ws = mesh_position_ws(Mesh.world_from_local, input.position);
    output.normal_ws = mesh_normal_ws(Mesh.world_from_local, input.normal);
    output.tex_coords = input.uv;
    output.tangent_ws = mesh_tangent_ws(Mesh.world_from_local, input.tangent);
    output.position = mesh_position_cs(View.clip_from_world, output.position_ws);
    return output;
}

[shader("fragment")]
FragmentOutput fragment_main(VertexOutput input, float4 frag_coord : SV_Position) {
    FragmentOutput output;
    output.position_ao = float4(input.position_ws, 1.0);
    output.normal_roughness.rgb =
        (Material.flags & STANDARD_MATERIAL_FLAGS_NORMAL_MAP_BIT) != 0 ?
        normal_mapping(input) :
        normalize(input.normal_ws);
    output.normal_roughness.a =
        (Material.flags & STANDARD_MATERIAL_FLAGS_ROUGHNESS_MAP_BIT) != 0 ?
        roughness_map.Sample(sampler, input.tex_coords).r :
        Material.roughness;
    output.normal_roughness.a = clamp(output.normal_roughness.a, 0.05, 0.96);
    output.albedo_metallic.rgb =
        (Material.flags & STANDARD_MATERIAL_FLAGS_ALBEDO_MAP_BIT) != 0 ?
        albedo_map.Sample(sampler, input.tex_coords).rgb :
        Material.albedo;
    output.albedo_metallic.a =
        (Material.flags & STANDARD_MATERIAL_FLAGS_METALLIC_MAP_BIT) != 0 ?
        metallic_map.Sample(sampler, input.tex_coords).r :
        Material.metallic;
    output.specular.rgb =
        (Material.flags & STANDARD_MATERIAL_FLAGS_SPECULAR_MAP_BIT) != 0 ?
        specular_map.Sample(sampler, input.tex_coords).rgb :
        Material.specular;
    output.specular.a = 1.0;
    output.emissive_depth.rgb =
        (Material.flags & STANDARD_MATERIAL_FLAGS_EMISSIVE_MAP_BIT) != 0 ?
        emissive_map.Sample(sampler, input.tex_coords).rgb :
        Material.emissive;
    output.emissive_depth.a = frag_coord.z;
    return output;
}
