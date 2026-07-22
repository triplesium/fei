import rendering.color;
import rendering.fullscreen;

using namespace rendering.color;

static const uint visualization_hdr_color = 0;
static const uint visualization_linear_color = 1;
static const uint visualization_scalar_alpha = 2;
static const uint visualization_normal = 3;
static const uint visualization_position = 4;
static const uint visualization_depth = 5;
static const uint present_flag_geometry_mask = 1;

struct PresentSettings {
    uint visualization;
    float exposure;
    float scalar_scale;
    float scalar_bias;
    uint flags;
    float3 padding;
};

layout(set = 0, binding = 0) Texture2D<float4> source;
layout(set = 0, binding = 1) Texture2D<float4> geometry_mask;
layout(set = 0, binding = 2) SamplerState source_sampler;
layout(set = 0, binding = 3) ConstantBuffer<PresentSettings> settings;

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
    if ((settings.flags & present_flag_geometry_mask) != 0 &&
        length(geometry_mask.Sample(source_sampler, input.tex_coords).xyz) <
            0.0001) {
        return float4(0.0, 0.0, 0.0, 1.0);
    }

    float4 value = source.Sample(source_sampler, input.tex_coords);
    float3 color;
    switch (settings.visualization) {
        case visualization_linear_color:
            color = linear_to_srgb(saturate(value.rgb));
            break;
        case visualization_scalar_alpha:
            color = saturate(
                value.a * settings.scalar_scale + settings.scalar_bias
            ).xxx;
            break;
        case visualization_normal:
            color = length(value.xyz) > 0.0001 ?
                        normalize(value.xyz) * 0.5 + 0.5 :
                        float3(0.0);
            break;
        case visualization_position:
            float3 compressed = value.xyz / (abs(value.xyz) + float3(1.0));
            color = compressed * 0.5 + 0.5;
            break;
        case visualization_depth:
            color = visualize_depth(value.a).xxx;
            break;
        default:
            color = tonemap_reinhard_to_srgb(
                max(value.rgb * settings.exposure, float3(0.0))
            );
            break;
    }
    return float4(saturate(color), 1.0);
}
