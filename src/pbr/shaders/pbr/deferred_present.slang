import rendering.color;
import rendering.fullscreen;

using namespace rendering.color;

layout(set = 0, binding = 0) Texture2D<float4> composite;
layout(set = 0, binding = 1) SamplerState composite_sampler;

[shader("fragment")]
float4 fragment_main(FullscreenFragmentInput input) : SV_Target0 {
    float3 hdr_color = composite.Sample(composite_sampler, input.tex_coords).rgb;
    return float4(tonemap_reinhard_to_srgb(hdr_color), 1.0);
}
