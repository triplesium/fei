import pbr.deferred.gbuffer;
import pbr.lib.brdf;
import pbr.material.types;
import pbr.shading.types;
import rendering.fullscreen;
import rendering.view;
import pbr.lighting.types;
import pbr.lighting.evsm;

using namespace pbr.brdf;
using namespace pbr.deferred;
using namespace pbr.lighting.evsm;

layout(set = 0, binding = 0) ConstantBuffer<ViewUniform> view;
layout(set = 1) ParameterBlock<GBuffer> gbuffer;
layout(set = 2, binding = 0) ConstantBuffer<LightingUniform> lighting;
layout(set = 2, binding = 1) Texture2D<float4> shadow_map;
layout(set = 2, binding = 2) SamplerState shadow_map_sampler;

float3 calculate_directional(
    Light light,
    ShadingPoint point,
    StandardMaterial material
) {
    float light_visibility =
        visibility(
            shadow_map,
            shadow_map_sampler,
            lighting.light_view_projection,
            point.position_ws
        );
    if (light_visibility <= 0.0) {
        return float3(0.0);
    }

    LightSample light_sample = LightSample.from_radiance(
        normalize(light.direction),
        max(light.diffuse, float3(0.0))
    );

    return evaluate_cook_torrance(point, material, light_sample) *
        light_visibility;
}

float3 calculate_point(
    Light light,
    ShadingPoint point,
    StandardMaterial material
) {
    float3 light_dir = light.position - point.position_ws;
    float d = length(light_dir);
    light_dir = normalize(light_dir);
    float falloff = light.attenuation.falloff(d);
    if (falloff <= 0.0) {
        return float3(0.0);
    }

    LightSample light_sample = LightSample.from_radiance(
        light_dir,
        max(light.diffuse, float3(0.0)) * falloff
    );

    return evaluate_cook_torrance(point, material, light_sample);
}

float3 calculate_spot(
    Light light,
    ShadingPoint point,
    StandardMaterial material
) {
    float3 light_vector = light.position - point.position_ws;
    float dst = length(light_vector);
    float3 light_dir = normalize(light_vector);
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

    float falloff = light.attenuation.falloff(dst);
    if (falloff <= 0.0) {
        return float3(0.0);
    }

    LightSample light_sample = LightSample.from_radiance(
        light_dir,
        max(light.diffuse, float3(0.0)) * falloff * spot_falloff
    );

    return evaluate_cook_torrance(point, material, light_sample);
}

float3 calculate_direct_lighting(GBufferSurface surface) {
    float3 direct_lighting = float3(0.0);
    float3 view_dir = view.view_direction(surface.position);
    ShadingPoint point = surface.shading_point(view_dir);
    StandardMaterial material = surface.material;

    for (uint i = 0; i < lighting.num_directional_lights; ++i) {
        direct_lighting += calculate_directional(
            lighting.directional_lights[i],
            point,
            material
        );
    }

    for (uint i = 0; i < lighting.num_point_lights; ++i) {
        direct_lighting += calculate_point(
            lighting.point_lights[i],
            point,
            material
        );
    }

    for (uint i = 0; i < lighting.num_spot_lights; ++i) {
        direct_lighting += calculate_spot(
            lighting.spot_lights[i],
            point,
            material
        );
    }

    return direct_lighting;
}

[shader("fragment")]
float4 fragment_main(FullscreenFragmentInput input) : SV_Target0 {
    GBufferSurface surface = gbuffer.read_lighting_surface(input.tex_coords);
    float3 direct_lighting = calculate_direct_lighting(surface);
    return float4(direct_lighting, 1.0);
}
