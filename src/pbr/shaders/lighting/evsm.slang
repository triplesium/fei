import lib.constants;

namespace pbr.lighting.evsm {

using namespace pbr.constants;

public static const float2 EXPONENTS = float2(40.0, 5.0);
public static const float LIGHT_BLEEDING_REDUCTION = 0.0;

public float linstep(float low, float high, float value) {
    return clamp((value - low) / (high - low), 0.0, 1.0);
}

public float reduce_light_bleeding(float p_max, float amount) {
    return linstep(amount, 1.0, p_max);
}

public float2 warp_depth(float depth) {
    depth = 2.0 * depth - 1.0;
    float pos = exp(EXPONENTS.x * depth);
    float neg = -exp(-EXPONENTS.y * depth);
    return float2(pos, neg);
}

public float4 depth_to_moments(float depth) {
    float2 moment1 = warp_depth(depth);
    float2 moment2 = moment1 * moment1;
    return float4(moment1, moment2);
}

public float chebyshev(float2 moments, float mean, float min_variance) {
    if (mean <= moments.x) {
        return 1.0;
    }

    float variance = moments.y - moments.x * moments.x;
    variance = max(variance, min_variance);
    float d = mean - moments.x;
    float lit = variance / (variance + d * d);
    return reduce_light_bleeding(lit, LIGHT_BLEEDING_REDUCTION);
}

public float visibility_from_moments(float4 moments, float depth) {
    if (dot(abs(moments), float4(1.0)) <= EPSILON) {
        return 1.0;
    }

    float2 warped_depth = warp_depth(depth - 0.0001);
    float2 depth_scale = 0.0001 * EXPONENTS * warped_depth;
    float2 min_variance = depth_scale * depth_scale;
    float positive = chebyshev(moments.xz, warped_depth.x, min_variance.x);
    float negative = chebyshev(moments.yw, warped_depth.y, min_variance.y);
    return min(positive, negative);
}

public bool shadow_uv_depth(
    float4x4 light_view_projection,
    float3 position,
    out float2 uv,
    out float depth
) {
    float4 light_space_position =
        mul(light_view_projection, float4(position, 1.0));
    if (abs(light_space_position.w) <= EPSILON) {
        return false;
    }

    light_space_position /= light_space_position.w;
    if (light_space_position.z < -1.0 || light_space_position.z > 1.0) {
        return false;
    }

    uv = light_space_position.xy * 0.5 + 0.5;
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) {
        return false;
    }

    depth = light_space_position.z * 0.5 + 0.5;
    return true;
}

public float visibility(
    Texture2D<float4> shadow_map,
    SamplerState shadow_map_sampler,
    float4x4 light_view_projection,
    float3 position
) {
    float2 uv = float2(0.0);
    float depth = 0.0;
    if (!shadow_uv_depth(light_view_projection, position, uv, depth)) {
        return 1.0;
    }

    float4 moments = shadow_map.Sample(shadow_map_sampler, uv);
    return visibility_from_moments(moments, depth);
}

public float visibility_level(
    Texture2D<float4> shadow_map,
    SamplerState shadow_map_sampler,
    float4x4 light_view_projection,
    float3 position
) {
    float2 uv = float2(0.0);
    float depth = 0.0;
    if (!shadow_uv_depth(light_view_projection, position, uv, depth)) {
        return 1.0;
    }

    float4 moments = shadow_map.SampleLevel(shadow_map_sampler, uv, 0.0);
    return visibility_from_moments(moments, depth);
}

} // namespace pbr.lighting.evsm
