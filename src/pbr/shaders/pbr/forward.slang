import pbr.lib.environment_map;
import rendering.color;
import rendering.view;
import pbr.material.types;
import pbr.shading.types;
import rendering.mesh;
import rendering.vertex_input;
import pbr.forward.io;

using namespace pbr.forward_io;
using namespace pbr.environment_map;
using namespace rendering.color;

layout(set = 0, binding = 0) ConstantBuffer<ViewUniform> view;
layout(set = 1, binding = 0) ConstantBuffer<MeshUniform> mesh;
layout(set = 2) ParameterBlock<StandardMaterialResources> material;
layout(set = 3) ParameterBlock<EnvironmentMap> environment;

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
#ifdef VERTEX_TANGENTS
    output.tangent_ws = mesh.tangent_ws(input.tangent);
#endif
    output.position = view.position_cs(output.position_ws);
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

    float3 view_direction = view.view_direction(input.position_ws);
    ShadingPoint point =
        ShadingPoint.from_world(input.position_ws, normal, view_direction);
    EnvironmentMapLighting environment_lighting = evaluate_environment_map(
        environment,
        point,
        surface_material
    );

    float3 color = tonemap_reinhard_to_srgb(environment_lighting.combined());
    output.color = float4(color, 1.0);
    return output;
}
