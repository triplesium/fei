import pbr.lib.brdf;
import rendering.color;
import rendering.view;
import pbr.material.types;
import pbr.shading.types;
import rendering.mesh;
import rendering.vertex_input;
import pbr.forward.io;

using namespace pbr.forward_io;
using namespace pbr.brdf;
using namespace rendering.color;

layout(set = 0, binding = 0) ConstantBuffer<ViewUniform> View;
layout(set = 0, binding = 1) TextureCube<float4> irradiance_map;
layout(set = 0, binding = 2) TextureCube<float4> radiance_map;
layout(set = 0, binding = 3) SamplerState cubemap_sampler;
layout(set = 0, binding = 4) Texture2D<float4> brdf_lut;
layout(set = 1, binding = 0) ConstantBuffer<MeshUniform> Mesh;
layout(set = 2) ParameterBlock<StandardMaterialResources> material;

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

    StandardMaterial surface_material = material.sample(tex_coords);

    float3 normal = material.sample_normal_from_derivatives(
        tex_coords,
        input.position_ws,
        input.normal_ws
    );

    float3 view = View.view_direction(input.position_ws);
    ShadingPoint point = ShadingPoint.from_world(input.position_ws, normal, view);
    float normal_dot_view = point.normal_dot_view;
    float3 reflection = point.reflection_ws;

    float3 fresnel = fresnel_schlick_roughness(
        normal_dot_view,
        surface_material.f0,
        surface_material.roughness
    );

    float3 k_s = fresnel;
    float3 k_d = (float3(1.0) - k_s) * (1.0 - surface_material.metallic);

    float3 diffuse =
        irradiance_map.Sample(cubemap_sampler, normal).rgb *
        surface_material.albedo;

    uint width;
    uint height;
    uint levels;
    radiance_map.GetDimensions(0, width, height, levels);
    float3 prefiltered_color =
        radiance_map.SampleLevel(
            cubemap_sampler,
            reflection,
            surface_material.roughness * float(levels)
        ).rgb;
    float2 brdf =
        brdf_lut.Sample(
            material.sampler,
            float2(normal_dot_view, surface_material.roughness)
        ).rg;
    float3 specular = prefiltered_color * (fresnel * brdf.x + brdf.y);
    float3 ambient = k_d * diffuse + specular;

    float3 color = tonemap_reinhard_to_srgb(ambient);
    output.color = float4(color, 1.0);
    return output;
}
