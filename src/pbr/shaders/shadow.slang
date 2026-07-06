#include "mesh_common.slangh"

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

static const float2 exponents = float2(40.0, 5.0);

float2 warp_depth(float depth) {
    depth = 2.0 * depth - 1.0;
    float pos = exp(exponents.x * depth);
    float neg = -exp(-exponents.y * depth);
    return float2(pos, neg);
}

float4 shadow_depth_to_evsm(float depth) {
    float2 moment1 = warp_depth(depth);
    float2 moment2 = moment1 * moment1;
    return float4(moment1, moment2);
}

[shader("vertex")]
VertexOutput vertex_main(VertexInput input) {
    VertexOutput output;
    float4 position_ws = mesh_position_ws4(Mesh.world_from_local, input.position);
    output.position = mesh_position_cs(View.clip_from_world, position_ws);
    output.tex_coords = input.uv;
    return output;
}

[shader("fragment")]
float4 fragment_main(VertexOutput input, float4 frag_coord : SV_Position) : SV_Target0 {
    return shadow_depth_to_evsm(frag_coord.z);
}
