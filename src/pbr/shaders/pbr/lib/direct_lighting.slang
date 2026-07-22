import pbr.lib.brdf;
import pbr.lighting.evsm;
import pbr.lighting.types;
import pbr.material.types;
import pbr.shading.types;

namespace pbr.direct_lighting {

using namespace pbr.brdf;
using namespace pbr.lighting.evsm;

public struct DirectLighting {
    public LightingUniform uniform;
    public Texture2D<float4> shadow_map;
    public SamplerState shadow_map_sampler;

    float3 calculate_directional(
        Light light,
        ShadingPoint point,
        StandardMaterial material
    ) {
        float light_visibility = visibility(
            shadow_map,
            shadow_map_sampler,
            uniform.light_view_projection,
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
        float light_distance = length(light_dir);
        light_dir = normalize(light_dir);
        float falloff = light.attenuation.falloff(light_distance);
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
        float light_distance = length(light_vector);
        float3 light_dir = normalize(light_vector);
        float cos_angle = dot(-light_dir, light.direction);
        if (cos_angle < light.angle_outer_cone) {
            return float3(0.0);
        }

        float inner_minus_outer =
            light.angle_inner_cone - light.angle_outer_cone;
        float spot_mark =
            (cos_angle - light.angle_outer_cone) / inner_minus_outer;
        float spot_falloff = smoothstep(0.0, 1.0, spot_mark);
        if (spot_falloff <= 0.0) {
            return float3(0.0);
        }

        float falloff = light.attenuation.falloff(light_distance);
        if (falloff <= 0.0) {
            return float3(0.0);
        }

        LightSample light_sample = LightSample.from_radiance(
            light_dir,
            max(light.diffuse, float3(0.0)) * falloff * spot_falloff
        );
        return evaluate_cook_torrance(point, material, light_sample);
    }

    public float3 evaluate(
        ShadingPoint point,
        StandardMaterial material
    ) {
        float3 result = float3(0.0);
        for (uint i = 0; i < uniform.num_directional_lights; ++i) {
            result += calculate_directional(
                uniform.directional_lights[i],
                point,
                material
            );
        }
        for (uint i = 0; i < uniform.num_point_lights; ++i) {
            result += calculate_point(
                uniform.point_lights[i],
                point,
                material
            );
        }
        for (uint i = 0; i < uniform.num_spot_lights; ++i) {
            result += calculate_spot(
                uniform.spot_lights[i],
                point,
                material
            );
        }
        return result;
    }
};

} // namespace pbr.direct_lighting
