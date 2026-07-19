import pbr.material.types;
import pbr.shading.types;

namespace pbr.environment_map {

public struct EnvironmentMapUniform {
    public float4x4 environment_from_world;
    public float intensity;
    public float max_specular_lod;
    public float2 padding;
};

public struct EnvironmentMapLighting {
    public float3 diffuse;
    public float3 specular;

    public float3 combined() {
        return diffuse + specular;
    }
};

struct MultiscatterTerms {
    float3 single_scatter;
    float3 multi_scatter;
};

bool is_finite_color(float3 value) {
    return all(value == value) && all(abs(value) < float3(1e20));
}

float3 safe_normalize(float3 value, float3 fallback) {
    float length_squared = dot(value, value);
    return length_squared > 1e-8 && length_squared < 1e20 ?
        value * rsqrt(length_squared) :
        fallback;
}

MultiscatterTerms compute_multiscatter(float3 f0, float2 f_ab) {
    float3 single_scatter = f0 * f_ab.x + f_ab.y;
    float3 average_fresnel = f0 + (float3(1.0) - f0) / 21.0;
    float missing_energy = saturate(1.0 - (f_ab.x + f_ab.y));
    float3 denominator = max(
        float3(1e-4),
        float3(1.0) - missing_energy * average_fresnel
    );
    float3 multi_scatter =
        single_scatter * average_fresnel / denominator * missing_energy;

    MultiscatterTerms terms;
    terms.single_scatter = max(single_scatter, float3(0.0));
    terms.multi_scatter = max(multi_scatter, float3(0.0));
    return terms;
}

float3 radiance_sample_direction(
    float3 normal,
    float3 reflection,
    float roughness
) {
    float smoothness = saturate(1.0 - roughness);
    float reflection_weight =
        smoothness * (sqrt(smoothness) + roughness);
    return safe_normalize(
        lerp(normal, reflection, reflection_weight),
        normal
    );
}

public EnvironmentMapLighting evaluate_environment_map(
    TextureCube<float4> irradiance_map,
    TextureCube<float4> radiance_map,
    SamplerState environment_sampler,
    Texture2D<float4> brdf_lut,
    SamplerState brdf_sampler,
    EnvironmentMapUniform environment,
    ShadingPoint point,
    StandardMaterial material
) {
    float3 normal = safe_normalize(
        mul(environment.environment_from_world, float4(point.normal_ws, 0.0)).xyz,
        float3(0.0, 1.0, 0.0)
    );
    float3 reflection = safe_normalize(
        mul(environment.environment_from_world, float4(point.reflection_ws, 0.0)).xyz,
        normal
    );
    float normal_dot_view = point.normal_dot_view;
    float3 irradiance =
        irradiance_map.Sample(environment_sampler, normal).rgb;
    float3 radiance_direction =
        radiance_sample_direction(normal, reflection, material.roughness);
    float3 radiance = radiance_map.SampleLevel(
        environment_sampler,
        radiance_direction,
        material.roughness * environment.max_specular_lod
    ).rgb;
    if (!is_finite_color(irradiance)) {
        irradiance = float3(0.0);
    }
    if (!is_finite_color(radiance)) {
        radiance = float3(0.0);
    }
    irradiance = max(irradiance, float3(0.0));
    radiance = max(radiance, float3(0.0));
    float2 brdf = brdf_lut.Sample(
        brdf_sampler,
        // The embedded LUT stores roughness from 1 at the top to 0 at the
        // bottom, while sampled texture coordinates increase downwards.
        float2(normal_dot_view, 1.0 - material.roughness)
    ).rg;
    if (!all(brdf == brdf) || any(abs(brdf) >= float2(1e20))) {
        brdf = float2(0.0);
    }
    MultiscatterTerms multiscatter =
        compute_multiscatter(material.f0, brdf);
    float3 diffuse_color =
        material.albedo * (1.0 - material.metallic);
    EnvironmentMapLighting lighting;
    lighting.diffuse = environment.intensity * irradiance * diffuse_color;
    lighting.specular =
        environment.intensity *
        (radiance * multiscatter.single_scatter +
         irradiance * multiscatter.multi_scatter);
    if (!is_finite_color(lighting.specular)) {
        lighting.specular = float3(0.0);
    }
    lighting.specular = max(lighting.specular, float3(0.0));
    return lighting;
}

} // namespace pbr.environment_map
