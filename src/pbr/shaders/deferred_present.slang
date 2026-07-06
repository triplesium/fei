struct FragmentInput {
    [vk::location(0)] float2 tex_coords : TEXCOORD0;
};

layout(set = 0, binding = 0) Texture2D<float4> composite;
layout(set = 0, binding = 1) SamplerState composite_sampler;

[shader("fragment")]
float4 fragment_main(FragmentInput input) : SV_Target0 {
    return composite.Sample(composite_sampler, input.tex_coords);
}
