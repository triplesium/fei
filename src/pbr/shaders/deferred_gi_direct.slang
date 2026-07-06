#include "common.slangh"

struct FragmentInput {
    [vk::location(0)] float2 tex_coords : TEXCOORD0;
};

layout(set = 0, binding = 0) ConstantBuffer<ViewData> View;
layout(set = 1, binding = 0) Texture2D<float4> g_position_ao;
layout(set = 1, binding = 1) Texture2D<float4> g_normal_roughness;
layout(set = 1, binding = 2) Texture2D<float4> g_albedo_metallic;
layout(set = 1, binding = 3) Texture2D<float4> g_specular;
layout(set = 1, binding = 5) SamplerState g_buffer_sampler;
layout(set = 2, binding = 0) ConstantBuffer<LightingData> Lighting;
layout(set = 2, binding = 1) Texture2D<float4> shadow_map;
layout(set = 2, binding = 2) SamplerState shadow_map_sampler;

static const float PI = 3.14159265;
static const float EPSILON = 1e-30;
static const float2 exponents = float2(40.0, 5.0);
static const float light_bleeding_reduction = 0.0;

float3 srgb_to_linear(float3 value) {
    return pow(max(value, float3(0.0)), float3(2.2));
}

float linstep(float low, float high, float value) {
    return clamp((value - low) / (high - low), 0.0, 1.0);
}

float reduce_light_bleeding(float p_max, float amount) {
    return linstep(amount, 1.0, p_max);
}

float2 warp_depth(float depth) {
    depth = 2.0 * depth - 1.0;
    float pos = exp(exponents.x * depth);
    float neg = -exp(-exponents.y * depth);
    return float2(pos, neg);
}

float chebyshev(float2 moments, float mean, float min_variance) {
    if (mean <= moments.x) {
        return 1.0;
    }

    float variance = moments.y - moments.x * moments.x;
    variance = max(variance, min_variance);
    float d = mean - moments.x;
    float lit = variance / (variance + d * d);
    return reduce_light_bleeding(lit, light_bleeding_reduction);
}

float visibility(float3 position) {
    float4 light_space_position =
        mul(Lighting.light_view_projection, float4(position, 1.0));
    if (abs(light_space_position.w) <= EPSILON) {
        return 1.0;
    }

    light_space_position /= light_space_position.w;
    if (light_space_position.z < -1.0 || light_space_position.z > 1.0) {
        return 1.0;
    }

    float2 uv = light_space_position.xy * 0.5 + 0.5;
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) {
        return 1.0;
    }

    float4 moments = shadow_map.Sample(shadow_map_sampler, uv);
    if (dot(abs(moments), float4(1.0)) <= EPSILON) {
        return 1.0;
    }

    float depth = light_space_position.z * 0.5 + 0.5;
    float2 warped_depth = warp_depth(depth - 0.0001);
    float2 depth_scale = 0.0001 * exponents * warped_depth;
    float2 min_variance = depth_scale * depth_scale;
    float positive = chebyshev(moments.xz, warped_depth.x, min_variance.x);
    float negative = chebyshev(moments.yw, warped_depth.y, min_variance.y);
    return min(positive, negative);
}

float distribution_ggx(float3 normal, float3 half_vector, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float n_dot_h = max(dot(normal, half_vector), 0.0);
    float n_dot_h2 = n_dot_h * n_dot_h;
    float denominator = n_dot_h2 * (a2 - 1.0) + 1.0;
    denominator = PI * denominator * denominator;
    return a2 / max(denominator, EPSILON);
}

float geometry_schlick_ggx(float n_dot_v, float roughness) {
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return n_dot_v / max(n_dot_v * (1.0 - k) + k, EPSILON);
}

float geometry_smith(float3 normal, float3 view, float3 light, float roughness) {
    float n_dot_v = max(dot(normal, view), 0.0);
    float n_dot_l = max(dot(normal, light), 0.0);
    return geometry_schlick_ggx(n_dot_v, roughness) *
        geometry_schlick_ggx(n_dot_l, roughness);
}

float3 fresnel_schlick(float cos_theta, float3 f0) {
    return f0 + (1.0 - f0) * pow(1.0 - cos_theta, 5.0);
}

float3 brdf(
    float3 normal,
    float3 view,
    float3 light,
    float3 radiance,
    float3 albedo,
    float metallic,
    float roughness,
    float3 f0
) {
    float3 half_vector = normalize(view + light);
    float n_dot_l = max(dot(normal, light), 0.0);
    float n_dot_v = max(dot(normal, view), 0.0);
    if (n_dot_l <= 0.0 || n_dot_v <= 0.0) {
        return float3(0.0);
    }

    float ndf = distribution_ggx(normal, half_vector, roughness);
    float geometry = geometry_smith(normal, view, light, roughness);
    float3 fresnel = fresnel_schlick(clamp(dot(half_vector, view), 0.0, 1.0), f0);

    float3 numerator = ndf * geometry * fresnel;
    float denominator = 4.0 * n_dot_v * n_dot_l + 0.001;
    float3 specular = numerator / denominator;

    float3 k_s = fresnel;
    float3 k_d = (float3(1.0) - k_s) * (1.0 - metallic);
    float3 diffuse = k_d * albedo / PI;
    return (diffuse + specular) * radiance * n_dot_l;
}

float3 calculate_directional(
    LightData light,
    float3 normal,
    float3 view_dir,
    float3 position,
    float3 albedo,
    float metallic,
    float roughness,
    float3 f0
) {
    float light_visibility = visibility(position);
    if (light_visibility <= 0.0) {
        return float3(0.0);
    }

    float3 light_dir = normalize(light.direction);
    float3 radiance = max(light.diffuse, float3(0.0));
    return brdf(normal, view_dir, light_dir, radiance, albedo, metallic, roughness, f0) *
        light_visibility;
}

float3 calculate_point(
    LightData light,
    float3 normal,
    float3 view_dir,
    float3 position,
    float3 albedo,
    float metallic,
    float roughness,
    float3 f0
) {
    float3 light_dir = light.position - position;
    float d = length(light_dir);
    light_dir = normalize(light_dir);
    float falloff = 1.0 /
        (light.attenuation.constant + light.attenuation.linear * d +
         light.attenuation.quadratic * d * d + 1.0);
    if (falloff <= 0.0) {
        return float3(0.0);
    }

    float3 radiance = max(light.diffuse, float3(0.0)) * falloff;
    return brdf(normal, view_dir, light_dir, radiance, albedo, metallic, roughness, f0);
}

float3 calculate_spot(
    LightData light,
    float3 normal,
    float3 view_dir,
    float3 position,
    float3 albedo,
    float metallic,
    float roughness,
    float3 f0
) {
    float3 light_dir = normalize(light.position - position);
    float cos_angle = dot(-light_dir, light.direction);
    if (cos_angle < light.angle_outer_cone) {
        return float3(0.0);
    }

    float inner_minus_outer = light.angle_inner_cone - light.angle_outer_cone;
    float spot_mark = (cos_angle - light.angle_outer_cone) / inner_minus_outer;
    float spot_falloff = smoothstep(0.0, 1.0, spot_mark);
    if (spot_falloff <= 0.0) {
        return float3(0.0);
    }

    float dst = distance(light.position, position);
    float falloff = 1.0 /
        (light.attenuation.constant + light.attenuation.linear * dst +
         light.attenuation.quadratic * dst * dst + 1.0);
    if (falloff <= 0.0) {
        return float3(0.0);
    }

    float3 radiance = max(light.diffuse, float3(0.0)) * falloff * spot_falloff;
    return brdf(normal, view_dir, light_dir, radiance, albedo, metallic, roughness, f0);
}

float3 calculate_direct_lighting(
    float3 position,
    float3 normal,
    float3 albedo,
    float metallic,
    float roughness,
    float3 f0
) {
    float3 direct_lighting = float3(0.0);
    float3 view_dir = normalize(View.world_position - position);

    for (uint i = 0; i < Lighting.num_directional_lights; ++i) {
        direct_lighting += calculate_directional(
            Lighting.directional_lights[i],
            normal,
            view_dir,
            position,
            albedo,
            metallic,
            roughness,
            f0
        );
    }

    for (uint i = 0; i < Lighting.num_point_lights; ++i) {
        direct_lighting += calculate_point(
            Lighting.point_lights[i],
            normal,
            view_dir,
            position,
            albedo,
            metallic,
            roughness,
            f0
        );
    }

    for (uint i = 0; i < Lighting.num_spot_lights; ++i) {
        direct_lighting += calculate_spot(
            Lighting.spot_lights[i],
            normal,
            view_dir,
            position,
            albedo,
            metallic,
            roughness,
            f0
        );
    }

    return direct_lighting;
}

[shader("fragment")]
float4 fragment_main(FragmentInput input) : SV_Target0 {
    float3 position = g_position_ao.Sample(g_buffer_sampler, input.tex_coords).rgb;
    float3 normal =
        normalize(g_normal_roughness.Sample(g_buffer_sampler, input.tex_coords).xyz);
    float roughness =
        clamp(g_normal_roughness.Sample(g_buffer_sampler, input.tex_coords).a, 0.045, 1.0);
    float3 base_color = g_albedo_metallic.Sample(g_buffer_sampler, input.tex_coords).rgb;
    float metallic =
        clamp(g_albedo_metallic.Sample(g_buffer_sampler, input.tex_coords).a, 0.0, 1.0);
    float3 albedo = srgb_to_linear(base_color);
    float3 specular_color = g_specular.Sample(g_buffer_sampler, input.tex_coords).rgb;
    float3 f0 = lerp(float3(0.04), albedo, metallic);
    f0 = lerp(f0, clamp(specular_color, 0.0, 1.0), 0.5);

    float3 direct_lighting =
        calculate_direct_lighting(position, normal, albedo, metallic, roughness, f0);
    return float4(direct_lighting, 1.0);
}
