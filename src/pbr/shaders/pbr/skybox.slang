import rendering.view;

struct VertexOutput {
    float4 position : SV_Position;
    [vk::location(0)] float3 tex_coords : TEXCOORD0;
};

layout(set = 0, binding = 0) ConstantBuffer<ViewUniform> View;
layout(set = 1, binding = 0) TextureCube<float4> skybox;
layout(set = 1, binding = 1) SamplerState skybox_sampler;

struct SkyboxUniform {
    float4x4 environment_from_world;
    float brightness;
};

layout(set = 1, binding = 2) ConstantBuffer<SkyboxUniform> Skybox;

[shader("vertex")]
VertexOutput vertex_main(uint vertex_id : SV_VertexID) {
    VertexOutput output;
    float2 clip_position =
        float2(vertex_id & 1u, (vertex_id >> 1u) & 1u) * 4.0 - 1.0;
    output.position = float4(clip_position, 1.0, 1.0);

    float4 view_position =
        mul(View.view_from_clip, float4(clip_position, 1.0, 1.0));
    float3 view_direction = view_position.xyz / view_position.w;
    float3 world_direction =
        mul(View.world_from_view, float4(view_direction, 0.0)).xyz;
    output.tex_coords =
        mul(Skybox.environment_from_world, float4(world_direction, 0.0)).xyz;
    return output;
}

[shader("fragment")]
float4 fragment_main(VertexOutput input) : SV_Target0 {
    return skybox.Sample(skybox_sampler, input.tex_coords) * Skybox.brightness;
}
