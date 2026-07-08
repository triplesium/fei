import lib.view;
import material.types;
import mesh.types;
import mesh.vertex_input;
import deferred.prepass_io;

using namespace pbr.prepass_io;

layout(set = 0, binding = 0) ConstantBuffer<ViewUniform> View;
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
FragmentOutput fragment_main(VertexOutput input, float4 frag_coord : SV_Position) {
    FragmentOutput output;
    float2 tex_coords = float2(0.0);
#ifdef VERTEX_UVS
    tex_coords = input.tex_coords;
#endif

    float3 tangent_ws = float3(1.0, 0.0, 0.0);
#ifdef VERTEX_TANGENTS
    tangent_ws = input.tangent_ws;
#endif

    output.position_ao = float4(input.position_ws, 1.0);

    output.normal_roughness.rgb = material.sample_normal_from_tangent_frame(
        tex_coords,
        input.normal_ws,
        tangent_ws
    );
    output.normal_roughness.a = material.sample_roughness(tex_coords);
    output.normal_roughness.a = clamp(output.normal_roughness.a, 0.05, 0.96);

    output.albedo_metallic.rgb = material.sample_albedo(tex_coords);
    output.albedo_metallic.a = material.sample_metallic(tex_coords);
    output.specular.rgb = material.sample_specular(tex_coords);
    output.specular.a = 1.0;

    output.emissive_depth.rgb = material.sample_emissive(tex_coords);
    output.emissive_depth.a = frag_coord.z;
    return output;
}
