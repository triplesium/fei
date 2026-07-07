#include "mesh_common.slangh"
#include "mesh_vertex_input.slangh"
#include "forward_io.slangh"

using namespace fei.pbr.forward_io;

layout(set = 0, binding = 0) ConstantBuffer<ViewData> View;
layout(set = 0, binding = 1) TextureCube<float4> irradiance_map;
layout(set = 0, binding = 2) TextureCube<float4> radiance_map;
layout(set = 0, binding = 3) SamplerState cubemap_sampler;
layout(set = 0, binding = 4) Texture2D<float4> brdf_lut;
layout(set = 1, binding = 0) ConstantBuffer<MeshData> Mesh;
layout(set = 2, binding = 0) ConstantBuffer<SimpleMaterialData> Material;
layout(set = 2, binding = 1) Texture2D<float4> albedo_map;
layout(set = 2, binding = 2) Texture2D<float4> normal_map;
layout(set = 2, binding = 3) Texture2D<float4> metallic_map;
layout(set = 2, binding = 4) Texture2D<float4> roughness_map;
layout(set = 2, binding = 7) SamplerState sampler;

static const float PI = 3.141592653589793;

#ifdef VERTEX_UVS
float3 get_normal_from_map(VertexOutput input) {
    float3 tangent_normal =
        normal_map.Sample(sampler, input.tex_coords).xyz * 2.0 - 1.0;

    float3 q1 = ddx(input.position_ws);
    float3 q2 = ddy(input.position_ws);
    float2 st1 = ddx(input.tex_coords);
    float2 st2 = ddy(input.tex_coords);

    float3 normal = normalize(input.normal_ws);
    float3 tangent = normalize(q1 * st2.y - q2 * st1.y);
    float3 bitangent = -normalize(cross(normal, tangent));
    float3x3 tbn = float3x3(tangent, bitangent, normal);
    return normalize(mul(tangent_normal, tbn));
}
#endif

float distribution_ggx(float3 normal, float3 half_vector, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float normal_dot_half = max(dot(normal, half_vector), 0.0);
    float normal_dot_half2 = normal_dot_half * normal_dot_half;
    float denominator = normal_dot_half2 * (a2 - 1.0) + 1.0;
    denominator = PI * denominator * denominator;
    return a2 / denominator;
}

float geometry_schlick_ggx(float normal_dot_view, float roughness) {
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return normal_dot_view / (normal_dot_view * (1.0 - k) + k);
}

float geometry_smith(float3 normal, float3 view, float3 light, float roughness) {
    float normal_dot_view = max(dot(normal, view), 0.0);
    float normal_dot_light = max(dot(normal, light), 0.0);
    return geometry_schlick_ggx(normal_dot_view, roughness) *
        geometry_schlick_ggx(normal_dot_light, roughness);
}

float3 fresnel_schlick(float cos_theta, float3 f0) {
    return f0 + (1.0 - f0) * pow(1.0 - cos_theta, 5.0);
}

float3 fresnel_schlick_roughness(float cos_theta, float3 f0, float roughness) {
    return f0 + (max(float3(1.0 - roughness), f0) - f0) *
        pow(clamp(1.0 - cos_theta, 0.0, 1.0), 5.0);
}

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
FragmentOutput fragment_main(VertexOutput input) {
    FragmentOutput output;
#if defined(STANDARD_MATERIAL_HAS_ALBEDO_MAP) && defined(VERTEX_UVS)
    float3 albedo = albedo_map.Sample(sampler, input.tex_coords).rgb;
#else
    float3 albedo = Material.albedo;
#endif

#if defined(STANDARD_MATERIAL_HAS_METALLIC_MAP) && defined(VERTEX_UVS)
    float metallic = metallic_map.Sample(sampler, input.tex_coords).r;
#else
    float metallic = Material.metallic;
#endif

#if defined(STANDARD_MATERIAL_HAS_ROUGHNESS_MAP) && defined(VERTEX_UVS)
    float roughness = roughness_map.Sample(sampler, input.tex_coords).r;
#else
    float roughness = Material.roughness;
#endif
    roughness = clamp(roughness, 0.01, 1.0);

#if defined(STANDARD_MATERIAL_HAS_NORMAL_MAP) && defined(VERTEX_UVS)
    float3 normal = get_normal_from_map(input);
#else
    float3 normal = normalize(input.normal_ws);
#endif

    float3 view = normalize(View.world_position - input.position_ws);
    float3 reflection = reflect(-view, normal);

    float3 f0 = lerp(float3(0.04), albedo, metallic);
    float3 fresnel =
        fresnel_schlick_roughness(max(dot(normal, view), 0.0), f0, roughness);

    float3 k_s = fresnel;
    float3 k_d = (float3(1.0) - k_s) * (1.0 - metallic);

    float3 diffuse = irradiance_map.Sample(cubemap_sampler, normal).rgb * albedo;

    uint width;
    uint height;
    uint levels;
    radiance_map.GetDimensions(0, width, height, levels);
    float3 prefiltered_color =
        radiance_map.SampleLevel(cubemap_sampler, reflection, roughness * float(levels)).rgb;
    float2 brdf =
        brdf_lut.Sample(sampler, float2(max(dot(normal, view), 0.0), roughness)).rg;
    float3 specular = prefiltered_color * (fresnel * brdf.x + brdf.y);
    float3 ambient = k_d * diffuse + specular;

    float3 color = ambient / (ambient + float3(1.0));
    color = pow(color, float3(1.0 / 2.2));
    output.color = float4(color, 1.0);
    return output;
}
