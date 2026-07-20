import pbr.deferred.gbuffer;
import rendering.fullscreen;

using namespace pbr.deferred;

layout(set = 1) ParameterBlock<GBuffer> gbuffer;
layout(set = 2, binding = 0) Texture2D<float4> direct_lighting;
layout(set = 2, binding = 1) Texture2D<float4> indirect_lighting;
layout(set = 2, binding = 2) SamplerState composite_sampler;

static const float normal_power = 64.0;
static const float position_sigma = 24.0;

float4 upsample_indirect_bilateral(float2 uv) {
    uint low_width;
    uint low_height;
    indirect_lighting.GetDimensions(low_width, low_height);
    float2 low_size = float2(low_width, low_height);
    float2 low_texel = 1.0 / low_size;

    float3 center_position = gbuffer.read_position(uv);
    float3 center_normal = gbuffer.read_normal(uv);

    float2 low_coord = uv * low_size - 0.5;
    float2 low_base = floor(low_coord);
    float2 frac_coord = frac(low_coord);

    float4 sum = float4(0.0);
    float wsum = 0.0;

    for (int j = 0; j <= 1; ++j) {
        for (int i = 0; i <= 1; ++i) {
            float2 p = (low_base + float2(i, j) + 0.5) * low_texel;
            p = clamp(p, float2(0.0), float2(1.0));

            float4 indirect = indirect_lighting.Sample(composite_sampler, p);
            float3 sample_position = gbuffer.read_position(p);
            float3 normal = gbuffer.read_normal(p);

            float pos_delta = length(sample_position - center_position);
            float w_depth = exp(-pos_delta * position_sigma);
            float w_normal =
                pow(max(dot(center_normal, normal), 0.0), normal_power);

            float wx = i == 0 ? 1.0 - frac_coord.x : frac_coord.x;
            float wy = j == 0 ? 1.0 - frac_coord.y : frac_coord.y;
            float w_spatial = wx * wy;

            float weight = w_spatial * w_depth * w_normal;
            sum += indirect * weight;
            wsum += weight;
        }
    }

    if (wsum > 1e-5) {
        return sum / wsum;
    }
    return indirect_lighting.Sample(composite_sampler, uv);
}

[shader("fragment")]
float4 fragment_main(FullscreenFragmentInput input) : SV_Target0 {
    float3 emissive = gbuffer.read_emissive(input.tex_coords);
    float3 direct_lighting_value =
        direct_lighting.Sample(composite_sampler, input.tex_coords).rgb;
    float3 indirect_lighting_value =
        upsample_indirect_bilateral(input.tex_coords).rgb;
    float3 composite_lighting =
        direct_lighting_value + indirect_lighting_value + emissive;

    return float4(composite_lighting, 1.0);
}
