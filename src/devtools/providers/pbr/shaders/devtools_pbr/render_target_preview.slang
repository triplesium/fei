import rendering.color;
import rendering.fullscreen;

namespace devtools_pbr {

using namespace rendering.color;

#ifndef DEVTOOLS_PBR_PREVIEW_MODE
#define DEVTOOLS_PBR_PREVIEW_MODE 0
#endif

layout(set = 0, binding = 0) Texture2D<float4> source;
layout(set = 0, binding = 1) Texture2D<float4> geometry_mask;
layout(set = 0, binding = 2) SamplerState source_sampler;

float visualize_depth(float depth) {
    static const float near_plane = 0.1;
    static const float far_plane = 1000.0;
    float z_ndc = depth * 2.0 - 1.0;
    float linear_depth =
        (2.0 * near_plane * far_plane) /
        (far_plane + near_plane - z_ndc * (far_plane - near_plane));
    float normalized_depth =
        saturate((linear_depth - near_plane) / (far_plane - near_plane));
    return 1.0 - pow(normalized_depth, 0.25);
}

[shader("fragment")]
float4 fragment_main(FullscreenFragmentInput input) : SV_Target0 {
    float4 value = source.Sample(source_sampler, input.tex_coords);
#if defined(DEVTOOLS_PBR_GEOMETRY_MASK)
    if (geometry_mask.Sample(source_sampler, input.tex_coords).a < 0.5) {
        return float4(0.0, 0.0, 0.0, 1.0);
    }
#endif

    float3 color;
#if DEVTOOLS_PBR_PREVIEW_MODE == 1
    color = value.aaa;
#elif DEVTOOLS_PBR_PREVIEW_MODE == 2
    float normal_length = length(value.xyz);
    color = normal_length > 0.0001 ?
                normalize(value.xyz) * 0.5 + 0.5 :
                float3(0.0);
#elif DEVTOOLS_PBR_PREVIEW_MODE == 3
    float3 compressed = value.xyz / (abs(value.xyz) + float3(1.0));
    color = compressed * 0.5 + 0.5;
#elif DEVTOOLS_PBR_PREVIEW_MODE == 4
    color = visualize_depth(value.a).xxx;
#elif DEVTOOLS_PBR_PREVIEW_MODE == 5
    color = tonemap_reinhard_to_srgb(max(value.rgb, float3(0.0)));
#else
    color = saturate(value.rgb);
#endif
    return float4(saturate(color), 1.0);
}

} // namespace devtools_pbr
