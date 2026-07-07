import lib.view;

struct VertexInput {
    [vk::location(0)] float3 position : POSITION;
};

struct VertexOutput {
    float4 position : SV_Position;
    [vk::location(0)] float3 tex_coords : TEXCOORD0;
};

layout(set = 0, binding = 0) ConstantBuffer<ViewData> View;
layout(set = 1, binding = 0) TextureCube<float4> skybox;
layout(set = 1, binding = 1) SamplerState skybox_sampler;

[shader("vertex")]
VertexOutput vertex_main(VertexInput input) {
    VertexOutput output;
    output.tex_coords = input.position;
    float3 view_position = mul((float3x3)View.view_from_world, input.position);
    float4 pos = mul(View.clip_from_view, float4(view_position, 1.0));
    output.position = pos.xyww;
    return output;
}

[shader("fragment")]
float4 fragment_main(VertexOutput input) : SV_Target0 {
    return skybox.Sample(skybox_sampler, input.tex_coords);
}
