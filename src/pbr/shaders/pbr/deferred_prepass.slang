import rendering.view;
import pbr.material.types;
import rendering.mesh;
import rendering.vertex_input;
import pbr.deferred.prepass_io;

using namespace pbr.prepass_io;

layout(set = 0, binding = 0) ConstantBuffer<ViewUniform> view;
layout(set = 1, binding = 0) ConstantBuffer<MeshUniform> mesh;
layout(set = 2) ParameterBlock<StandardMaterialResources> material;

[shader("vertex")]
VertexOutput vertex_main(MeshVertexInput input) {
    VertexOutput output;
    output.position_ws = mesh.position_ws(input.position);
#ifdef VERTEX_NORMALS
    output.normal_ws = mesh.normal_ws(input.normal);
#else
    output.normal_ws = float3(0.0, 1.0, 0.0);
#endif
#ifdef VERTEX_UVS
    output.tex_coords = input.uv;
#endif
#ifdef VERTEX_UVS_1
    output.tex_coords_1 = input.uv1;
#endif
#ifdef VERTEX_TANGENTS
    output.tangent_ws = mesh.tangent_ws(input.tangent);
#endif
#ifdef VERTEX_COLORS
    output.color = input.color;
#endif
    output.position = view.position_cs(output.position_ws);
    return output;
}

[shader("fragment")]
FragmentOutput fragment_main(VertexOutput input, float4 frag_coord : SV_Position) {
    FragmentOutput output;
    StandardMaterialTexCoords tex_coords;
    tex_coords.uv0 = float2(0.0);
    tex_coords.uv1 = float2(0.0);
#ifdef VERTEX_UVS
    tex_coords.uv0 = input.tex_coords;
#endif
#ifdef VERTEX_UVS_1
    tex_coords.uv1 = input.tex_coords_1;
#endif

#ifdef MAY_DISCARD
    float alpha = material.sample_alpha(tex_coords);
#ifdef VERTEX_COLORS
    alpha *= input.color.a;
#endif
    if (alpha < material.alpha_cutoff) {
        discard;
    }
#endif

    float3 tangent_ws = float3(1.0, 0.0, 0.0);
#ifdef VERTEX_TANGENTS
    tangent_ws = input.tangent_ws;
#endif

    output.position_ao =
        float4(input.position_ws, material.sample_occlusion(tex_coords));

    output.normal_roughness.rgb = material.sample_normal_from_tangent_frame(
        tex_coords,
        input.normal_ws,
        tangent_ws
    );
    output.normal_roughness.a = material.sample_roughness(tex_coords);
    output.normal_roughness.a = clamp(output.normal_roughness.a, 0.05, 0.96);

    output.albedo_metallic.rgb = material.sample_albedo(tex_coords);
#ifdef VERTEX_COLORS
    output.albedo_metallic.rgb *= input.color.rgb;
#endif
    output.albedo_metallic.a = material.sample_metallic(tex_coords);
    output.specular.rgb = material.sample_specular(tex_coords);
    output.specular.a = 1.0;

    output.emissive_depth.rgb = material.sample_emissive(tex_coords);
    output.emissive_depth.a = frag_coord.z;
    return output;
}
