import pbr.deferred.gbuffer;
import pbr.material.types;
import rendering.constants;
import rendering.fullscreen;
import rendering.view;
import pbr.lib.environment_map;
import pbr.shading.types;
import pbr.vxgi.basis;
import pbr.vxgi.types;

using namespace rendering.constants;
using namespace pbr.deferred;
using namespace pbr.environment_map;
using namespace pbr.vxgi;

layout(set = 0, binding = 0) ConstantBuffer<ViewUniform> view;
layout(set = 1) ParameterBlock<GBuffer> gbuffer;
layout(set = 2, binding = 0) ConstantBuffer<VxgiUniform> vxgi;
layout(set = 2, binding = 2) Texture3D<float4> voxel_tex;
layout(set = 2, binding = 3) Texture3D<float4> voxel_tex_mipmap[6];
layout(set = 2, binding = 9) SamplerState voxel_sampler;
layout(set = 3) ParameterBlock<EnvironmentMap> environment;

static const float3 diffuse_cone_directions[6] = {
    float3(0.0, 1.0, 0.0),
    float3(0.0, 0.5, 0.866025),
    float3(0.823639, 0.5, 0.267617),
    float3(0.509037, 0.5, -0.7006629),
    float3(-0.50937, 0.5, -0.7006629),
    float3(-0.823639, 0.5, 0.267617),
};

static const float diffuse_cone_weights[6] = {
    PI / 4.0,
    3.0 * PI / 20.0,
    3.0 * PI / 20.0,
    3.0 * PI / 20.0,
    3.0 * PI / 20.0,
    3.0 * PI / 20.0,
};

// The diffuse cones have a 30-degree half angle. Match that solid angle to a
// GGX-prefiltered environment mip (alpha ~= tan(15 degrees)).
static const float diffuse_cone_environment_roughness = 0.517638;

bool is_finite_indirect_color(float3 value) {
    return all(value == value) && all(abs(value) < float3(1e20));
}

bool is_finite_value(float value) {
    return value == value && abs(value) < 1e20;
}

struct ConeTraceResult {
    float3 radiance;
    float local_occlusion;
    float sky_opacity;
};

float4 anisotropic_sample(float3 coord, float3 weight, uint3 face, float lod) {
    float aniso_level = max(lod - 1.0, 0.0);
    float4 aniso_sample =
        weight.x * voxel_tex_mipmap[face.x].SampleLevel(voxel_sampler, coord, aniso_level) +
        weight.y * voxel_tex_mipmap[face.y].SampleLevel(voxel_sampler, coord, aniso_level) +
        weight.z * voxel_tex_mipmap[face.z].SampleLevel(voxel_sampler, coord, aniso_level);

    if (lod < 1.0) {
        float4 base_color = voxel_tex.Sample(voxel_sampler, coord);
        aniso_sample = lerp(base_color, aniso_sample, clamp(lod, 0.0, 1.0));
    }

    return aniso_sample;
}

ConeTraceResult trace_cone(
    float3 position,
    float3 normal,
    float3 direction,
    float aperture,
    bool trace_local_occlusion
) {
    ConeTraceResult result;
    result.radiance = float3(0.0);
    result.local_occlusion = 0.0;
    result.sky_opacity = 0.0;

    uint3 visible_face = visible_aniso_faces(direction);
    trace_local_occlusion =
        trace_local_occlusion && vxgi.ao_alpha < 1.0;

    float voxel_world_size = vxgi.cone_trace_voxel_world_size;
    float3 weight = direction * direction;
    float dst = voxel_world_size;
    float3 start_position = position + normal * dst;
    float radiance_opacity = 0.0;
    float radiance_max_distance =
        vxgi.max_tracing_distance_global * vxgi.inverse_voxel_scale;
    float falloff = 0.5 * vxgi.ao_falloff * vxgi.voxel_scale;
    float enter = 0.0;
    float leave = 0.0;

    if (!vxgi.intersect_world_aabb(start_position, direction, enter, leave)) {
        return result;
    }
    dst = max(dst, enter);
    radiance_max_distance = min(radiance_max_distance, leave);

    while (dst <= leave &&
           (result.sky_opacity < 1.0 ||
            (dst <= radiance_max_distance && radiance_opacity < 1.0))) {
        float3 cone_position = start_position + direction * dst;
        float diameter = 2.0 * aperture * dst;
        float mip_level = log2(diameter / voxel_world_size);
        float3 coord = vxgi.world_to_voxel(cone_position);
        float4 aniso_sample = anisotropic_sample(coord, weight, visible_face, mip_level);
        float sample_opacity = is_finite_value(aniso_sample.a) ?
            saturate(aniso_sample.a) :
            0.0;

        if (dst <= radiance_max_distance && radiance_opacity < 1.0) {
            result.radiance +=
                (1.0 - radiance_opacity) * aniso_sample.rgb;
            radiance_opacity +=
                (1.0 - radiance_opacity) * sample_opacity;
        }

        if (trace_local_occlusion && result.local_occlusion < 1.0) {
            result.local_occlusion +=
                ((1.0 - result.local_occlusion) * sample_opacity) /
                (1.0 + falloff * diameter);
        }
        result.sky_opacity +=
            (1.0 - result.sky_opacity) * sample_opacity;

        dst += diameter * vxgi.sampling_factor;
    }

    result.local_occlusion = saturate(result.local_occlusion);
    result.sky_opacity = saturate(result.sky_opacity);
    return result;
}

float compute_specular_visibility(
    float normal_dot_view,
    float visibility,
    float roughness
) {
    float exponent = exp2(-16.0 * saturate(roughness) - 1.0);
    return saturate(
        pow(saturate(normal_dot_view) + saturate(visibility), exponent) -
        1.0 + visibility
    );
}

float4 calculate_indirect_lighting(
    GBufferSurface surface,
    bool ambient_occlusion
) {
    float3 diffuse_radiance = float3(0.0);
    float3 environment_miss_radiance = float3(0.0);
    float3 unoccluded_environment_radiance = float3(0.0);
    float weighted_local_occlusion = 0.0;
    float weighted_sky_transmittance = 0.0;
    float3 cone_direction = float3(0.0);
    float3 view_direction = view.view_direction(surface.position);
    ShadingPoint point = surface.shading_point(view_direction);
    StandardMaterial material = surface.material;
    bool has_lighting_surface =
        surface.has_albedo() || surface.has_specular();

    if (has_lighting_surface) {
        const float aperture = 0.57735;
        OrthonormalBasis basis = OrthonormalBasis.from_normal(surface.normal);

        for (int i = 0; i < 6; i++) {
            cone_direction =
                basis.cone_direction(surface.normal, diffuse_cone_directions[i]);
            ConeTraceResult trace = trace_cone(
                surface.position,
                surface.normal,
                cone_direction,
                aperture,
                ambient_occlusion
            );
            float cone_weight = diffuse_cone_weights[i];
            float sky_transmittance = saturate(1.0 - trace.sky_opacity);
            float environment_transmittance = ambient_occlusion ?
                lerp(
                    saturate(vxgi.skylight_leaking),
                    1.0,
                    sky_transmittance
                ) :
                1.0;
            diffuse_radiance += trace.radiance * cone_weight;
            if (environment.enabled) {
                float3 cone_environment_radiance =
                    sample_environment_radiance(
                        environment,
                        cone_direction,
                        diffuse_cone_environment_roughness
                    );
                environment_miss_radiance +=
                    environment_transmittance *
                    cone_environment_radiance *
                    cone_weight;
                unoccluded_environment_radiance +=
                    cone_environment_radiance * cone_weight;
            }
            weighted_local_occlusion +=
                trace.local_occlusion * cone_weight;
            weighted_sky_transmittance +=
                sky_transmittance * cone_weight;
        }

        float3 diffuse_color =
            material.albedo * (1.0 - material.metallic);
        diffuse_radiance *= diffuse_color / PI;
    }

    EnvironmentMapLighting environment_lighting;
    environment_lighting.diffuse = float3(0.0);
    environment_lighting.specular = float3(0.0);
    if (has_lighting_surface) {
        environment_lighting = evaluate_environment_map(
            environment,
            point,
            material
        );
    }

    float sky_visibility = 1.0;
    float local_visibility = 1.0;
    if (ambient_occlusion && has_lighting_surface) {
        if (!is_finite_value(weighted_sky_transmittance)) {
            weighted_sky_transmittance = PI;
        }
        if (!is_finite_value(weighted_local_occlusion)) {
            weighted_local_occlusion = 0.0;
        }
        sky_visibility = saturate(weighted_sky_transmittance / PI);
        local_visibility =
            1.0 - saturate(weighted_local_occlusion / PI);
        sky_visibility = lerp(
            saturate(vxgi.skylight_leaking),
            1.0,
            sky_visibility
        );
    }

    float specular_visibility = compute_specular_visibility(
        point.normal_dot_view,
        min(sky_visibility, local_visibility),
        material.roughness
    );

    float3 vxgi_diffuse = vxgi.bounce_strength * diffuse_radiance;
    if (!is_finite_indirect_color(vxgi_diffuse)) {
        vxgi_diffuse = float3(0.0);
    }
    vxgi_diffuse = max(vxgi_diffuse, float3(0.0));
    float3 environment_visibility = float3(1.0);
    if (has_lighting_surface && environment.enabled) {
        if (!is_finite_indirect_color(environment_miss_radiance) ||
            !is_finite_indirect_color(unoccluded_environment_radiance)) {
            environment_miss_radiance = float3(0.0);
            unoccluded_environment_radiance = float3(0.0);
        }
        // Preserve the irradiance cubemap's diffuse energy while using the
        // cone misses to make its visibility directional.
        environment_visibility = saturate(
            environment_miss_radiance /
            max(unoccluded_environment_radiance, float3(1e-4))
        );
    }
    float3 environment_diffuse =
        environment_lighting.diffuse * environment_visibility;
    float3 result =
        environment_diffuse +
        environment_lighting.specular * specular_visibility +
        vxgi_diffuse;
    if (!is_finite_indirect_color(result)) {
        result = environment_diffuse;
    }
    result = max(result, float3(0.0));
    float diffuse_sky_visibility = dot(
        environment_visibility,
        float3(0.2126, 0.7152, 0.0722)
    );
    return float4(result, saturate(diffuse_sky_visibility));
}

[shader("fragment")]
float4 fragment_main(FullscreenFragmentInput input) : SV_Target0 {
    GBufferSurface surface = gbuffer.read_lighting_surface(input.tex_coords);
    return calculate_indirect_lighting(surface, true);
}
