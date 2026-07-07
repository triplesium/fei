#include "mesh_common.slangh"
#include "mesh_vertex_input.slangh"
#include "prepass_io.slangh"

using namespace fei.pbr.prepass_io;

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

#if defined(VERTEX_UVS) && defined(VERTEX_TANGENTS)
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
#endif

[shader("vertex")]
VertexOutput vertex_main(MeshVertexInput input) {
    VertexOutput output;
    output.position_ws = mesh_position_ws(Mesh.world_from_local, input.position);
#ifdef VERTEX_NORMALS
    output.normal_ws = mesh_normal_ws(Mesh.world_from_local, input.normal);
#else
    output.normal_ws = float3(0.0, 1.0, 0.0);
#endif
#ifdef VERTEX_UVS
    output.tex_coords = input.uv;
#endif
#ifdef VERTEX_TANGENTS
    output.tangent_ws = mesh_tangent_ws(Mesh.world_from_local, input.tangent);
#endif
    output.position = mesh_position_cs(View.clip_from_world, output.position_ws);
    return output;
}

[shader("fragment")]
FragmentOutput fragment_main(VertexOutput input, float4 frag_coord : SV_Position) {
    FragmentOutput output;
    output.position_ao = float4(input.position_ws, 1.0);
#if defined(STANDARD_MATERIAL_HAS_NORMAL_MAP) && defined(VERTEX_UVS) && defined(VERTEX_TANGENTS)
    output.normal_roughness.rgb = normal_mapping(input);
#else
    output.normal_roughness.rgb = normalize(input.normal_ws);
#endif

#if defined(STANDARD_MATERIAL_HAS_ROUGHNESS_MAP) && defined(VERTEX_UVS)
    output.normal_roughness.a = roughness_map.Sample(sampler, input.tex_coords).r;
#else
    output.normal_roughness.a = Material.roughness;
#endif
    output.normal_roughness.a = clamp(output.normal_roughness.a, 0.05, 0.96);

#if defined(STANDARD_MATERIAL_HAS_ALBEDO_MAP) && defined(VERTEX_UVS)
    output.albedo_metallic.rgb = albedo_map.Sample(sampler, input.tex_coords).rgb;
#else
    output.albedo_metallic.rgb = Material.albedo;
#endif

#if defined(STANDARD_MATERIAL_HAS_METALLIC_MAP) && defined(VERTEX_UVS)
    output.albedo_metallic.a = metallic_map.Sample(sampler, input.tex_coords).r;
#else
    output.albedo_metallic.a = Material.metallic;
#endif

#if defined(STANDARD_MATERIAL_HAS_SPECULAR_MAP) && defined(VERTEX_UVS)
    output.specular.rgb = specular_map.Sample(sampler, input.tex_coords).rgb;
#else
    output.specular.rgb = Material.specular;
#endif
    output.specular.a = 1.0;

#if defined(STANDARD_MATERIAL_HAS_EMISSIVE_MAP) && defined(VERTEX_UVS)
    output.emissive_depth.rgb = emissive_map.Sample(sampler, input.tex_coords).rgb;
#else
    output.emissive_depth.rgb = Material.emissive;
#endif
    output.emissive_depth.a = frag_coord.z;
    return output;
}
