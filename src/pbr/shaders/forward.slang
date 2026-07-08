import lib.brdf;
import lib.color;
import lib.view;
import material.types;
import mesh.types;
import mesh.vertex_input;
import forward.io;

using namespace pbr.forward_io;
using namespace pbr.brdf;
using namespace pbr.color;

layout(set = 0, binding = 0) ConstantBuffer<ViewUniform> View;
layout(set = 0, binding = 1) TextureCube<float4> irradiance_map;
layout(set = 0, binding = 2) TextureCube<float4> radiance_map;
layout(set = 0, binding = 3) SamplerState cubemap_sampler;
layout(set = 0, binding = 4) Texture2D<float4> brdf_lut;
layout(set = 1, binding = 0) ConstantBuffer<MeshUniform> Mesh;
layout(set = 2) ParameterBlock<StandardMaterialBlock> material;

[shader("vertex")]
VertexOutput vertex_main(MeshVertexInput input) {
    VertexOutput output;
    output.position_ws = Mesh.position_ws(input.position);
#ifdef VERTEX_NORMALS
    output.normal_ws = Mesh.normal_ws(input.normal);
#else
    output.normal_ws = float3(0.0, 1.0, 0.0);
#endif
#ifdef VERTEX_UVS
    output.tex_coords = input.uv;
#endif
#ifdef VERTEX_TANGENTS
    output.tangent_ws = Mesh.tangent_ws(input.tangent);
#endif
    output.position = View.position_cs(output.position_ws);
    return output;
}

[shader("fragment")]
FragmentOutput fragment_main(VertexOutput input) {
    FragmentOutput output;
    float2 tex_coords = float2(0.0);
#ifdef VERTEX_UVS
    tex_coords = input.tex_coords;
#endif

    float3 albedo = material.sample_albedo(tex_coords);
    float metallic = material.sample_metallic(tex_coords);
    float roughness = material.sample_roughness(tex_coords);
    roughness = clamp(roughness, 0.01, 1.0);

    float3 normal = material.sample_normal_from_derivatives(
        tex_coords,
        input.position_ws,
        input.normal_ws
    );

    float3 view = View.view_direction(input.position_ws);
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
        brdf_lut.Sample(
            material.sampler,
            float2(max(dot(normal, view), 0.0), roughness)
        ).rg;
    float3 specular = prefiltered_color * (fresnel * brdf.x + brdf.y);
    float3 ambient = k_d * diffuse + specular;

    float3 color = tonemap_reinhard_to_srgb(ambient);
    output.color = float4(color, 1.0);
    return output;
}
