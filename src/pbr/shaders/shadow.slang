import lib.view;
import lighting.evsm;
import material.types;
import mesh.types;

using namespace pbr.lighting.evsm;

struct VertexInput {
    [vk::location(0)] float3 position : POSITION;
    [vk::location(2)] float2 uv : TEXCOORD0;
};

struct VertexOutput {
    float4 position : SV_Position;
    [vk::location(0)] float2 tex_coords : TEXCOORD0;
};

layout(set = 0, binding = 0) ConstantBuffer<ViewData> View;
layout(set = 1, binding = 0) ConstantBuffer<MeshData> Mesh;
layout(set = 2, binding = 0) ConstantBuffer<PbrMaterialData> Material;

[shader("vertex")]
VertexOutput vertex_main(VertexInput input) {
    VertexOutput output;
    float4 position_ws = Mesh.position_ws4(input.position);
    output.position = View.position_cs(position_ws);
    output.tex_coords = input.uv;
    return output;
}

[shader("fragment")]
float4 fragment_main(VertexOutput input, float4 frag_coord : SV_Position) : SV_Target0 {
    return depth_to_moments(frag_coord.z);
}
