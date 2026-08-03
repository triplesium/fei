import rendering.view;
import pbr.lighting.evsm;
import pbr.material.types;
import rendering.mesh;
import rendering.vertex_input;

using namespace pbr.lighting.evsm;

struct VertexOutput {
    float4 position : SV_Position;
#ifdef VERTEX_UVS
    [vk::location(0)] float2 tex_coords : TEXCOORD0;
#endif
#ifdef VERTEX_UVS_1
    [vk::location(1)] float2 tex_coords_1 : TEXCOORD1;
#endif
#ifdef VERTEX_COLORS
    [vk::location(2)] float4 color : COLOR2;
#endif
};

layout(set = 0, binding = 0) ConstantBuffer<ViewUniform> view;
layout(set = 1, binding = 0) ConstantBuffer<MeshUniform> mesh;
layout(set = 2) ParameterBlock<StandardMaterialResources> material;

[shader("vertex")]
VertexOutput vertex_main(MeshVertexInput input) {
    VertexOutput output;
    float4 position_ws = mesh.position_ws4(input.position);
    output.position = view.position_cs(position_ws);
#ifdef VERTEX_UVS
    output.tex_coords = input.uv;
#endif
#ifdef VERTEX_UVS_1
    output.tex_coords_1 = input.uv1;
#endif
#ifdef VERTEX_COLORS
    output.color = input.color;
#endif
    return output;
}

[shader("fragment")]
float4 fragment_main(VertexOutput input) : SV_Target0 {
#ifdef MAY_DISCARD
    StandardMaterialTexCoords tex_coords;
    tex_coords.uv0 = float2(0.0);
    tex_coords.uv1 = float2(0.0);
#ifdef VERTEX_UVS
    tex_coords.uv0 = input.tex_coords;
#endif
#ifdef VERTEX_UVS_1
    tex_coords.uv1 = input.tex_coords_1;
#endif
    float alpha = material.sample_alpha(tex_coords);
#ifdef VERTEX_COLORS
    alpha *= input.color.a;
#endif
    if (alpha < material.alpha_cutoff) {
        discard;
    }
#endif
    return depth_to_moments(input.position.z);
}
