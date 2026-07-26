struct SpriteView {
    float4x4 clip_from_world;
};

struct SpriteVertexInput {
    [vk::location(0)] float2 position : POSITION;
    [vk::location(1)] float2 uv : TEXCOORD0;
    [vk::location(2)] float4 color : COLOR0;
};

struct SpriteFragmentInput {
    float4 position : SV_Position;
    [vk::location(0)] float2 uv : TEXCOORD0;
    [vk::location(1)] float4 color : COLOR0;
};

layout(set = 0, binding = 0) ConstantBuffer<SpriteView> view;
layout(set = 1, binding = 0) Texture2D<float4> image;
layout(set = 1, binding = 1) SamplerState image_sampler;

[shader("vertex")]
SpriteFragmentInput vertex_main(SpriteVertexInput input) {
    SpriteFragmentInput output;
    output.position = mul(view.clip_from_world, float4(input.position, 0.0, 1.0));
    output.uv = input.uv;
    output.color = input.color;
    return output;
}

[shader("fragment")]
float4 fragment_main(SpriteFragmentInput input) : SV_Target0 {
    float4 color = image.Sample(image_sampler, input.uv) * input.color;
    color.rgb *= color.a;
    return color;
}
