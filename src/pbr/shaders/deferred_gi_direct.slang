import deferred.gbuffer;
import lib.brdf;
import lib.fullscreen;
import lib.view;
import lighting.types;
import lighting.evsm;

using namespace pbr.brdf;
using namespace pbr.deferred;
using namespace pbr.lighting.evsm;

layout(set = 0, binding = 0) ConstantBuffer<ViewData> View;
layout(set = 1, binding = 0) Texture2D<float4> g_position_ao;
layout(set = 1, binding = 1) Texture2D<float4> g_normal_roughness;
layout(set = 1, binding = 2) Texture2D<float4> g_albedo_metallic;
layout(set = 1, binding = 3) Texture2D<float4> g_specular;
layout(set = 1, binding = 5) SamplerState g_buffer_sampler;
layout(set = 2, binding = 0) ConstantBuffer<LightingData> Lighting;
layout(set = 2, binding = 1) Texture2D<float4> shadow_map;
layout(set = 2, binding = 2) SamplerState shadow_map_sampler;

float3 calculate_directional(
    LightData light,
    GBufferSurface surface,
    float3 view_dir
) {
    float light_visibility =
        visibility(
            shadow_map,
            shadow_map_sampler,
            Lighting.light_view_projection,
            surface.position
        );
    if (light_visibility <= 0.0) {
        return float3(0.0);
    }

    float3 light_dir = normalize(light.direction);
    float3 radiance = max(light.diffuse, float3(0.0));
    return evaluate_cook_torrance(
        surface.normal,
        view_dir,
        light_dir,
        radiance,
        surface.albedo,
        surface.metallic,
        surface.roughness,
        surface.f0
    ) * light_visibility;
}

float3 calculate_point(
    LightData light,
    GBufferSurface surface,
    float3 view_dir
) {
    float3 light_dir = light.position - surface.position;
    float d = length(light_dir);
    light_dir = normalize(light_dir);
    float falloff = 1.0 /
        (light.attenuation.constant + light.attenuation.linear * d +
         light.attenuation.quadratic * d * d + 1.0);
    if (falloff <= 0.0) {
        return float3(0.0);
    }

    float3 radiance = max(light.diffuse, float3(0.0)) * falloff;
    return evaluate_cook_torrance(
        surface.normal,
        view_dir,
        light_dir,
        radiance,
        surface.albedo,
        surface.metallic,
        surface.roughness,
        surface.f0
    );
}

float3 calculate_spot(
    LightData light,
    GBufferSurface surface,
    float3 view_dir
) {
    float3 light_dir = normalize(light.position - surface.position);
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

    float dst = distance(light.position, surface.position);
    float falloff = 1.0 /
        (light.attenuation.constant + light.attenuation.linear * dst +
         light.attenuation.quadratic * dst * dst + 1.0);
    if (falloff <= 0.0) {
        return float3(0.0);
    }

    float3 radiance = max(light.diffuse, float3(0.0)) * falloff * spot_falloff;
    return evaluate_cook_torrance(
        surface.normal,
        view_dir,
        light_dir,
        radiance,
        surface.albedo,
        surface.metallic,
        surface.roughness,
        surface.f0
    );
}

float3 calculate_direct_lighting(GBufferSurface surface) {
    float3 direct_lighting = float3(0.0);
    float3 view_dir = View.view_direction(surface.position);

    for (uint i = 0; i < Lighting.num_directional_lights; ++i) {
        direct_lighting += calculate_directional(
            Lighting.directional_lights[i],
            surface,
            view_dir
        );
    }

    for (uint i = 0; i < Lighting.num_point_lights; ++i) {
        direct_lighting += calculate_point(
            Lighting.point_lights[i],
            surface,
            view_dir
        );
    }

    for (uint i = 0; i < Lighting.num_spot_lights; ++i) {
        direct_lighting += calculate_spot(
            Lighting.spot_lights[i],
            surface,
            view_dir
        );
    }

    return direct_lighting;
}

[shader("fragment")]
float4 fragment_main(FullscreenFragmentInput input) : SV_Target0 {
    GBufferSurface surface = read_lighting_surface(
        g_position_ao,
        g_normal_roughness,
        g_albedo_metallic,
        g_specular,
        g_buffer_sampler,
        input.tex_coords
    );
    float3 direct_lighting = calculate_direct_lighting(surface);
    return float4(direct_lighting, 1.0);
}
