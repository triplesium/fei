import pbr.material.types;
import pbr.shading.types;

namespace pbr.environment_map {

public struct EnvironmentMapUniform {
    public float4x4 environment_from_world;
    public float intensity;
    public float max_specular_lod;
    public uint enabled;
    public float padding;
};

public struct EnvironmentMap {
    public EnvironmentMapUniform uniform;
    public TextureCube<float4> irradiance_map;
    public TextureCube<float4> radiance_map;
    public SamplerState cubemap_sampler;
    public Texture2D<float4> brdf_lut;
    public SamplerState brdf_sampler;

    public property bool enabled {
        get {
            return uniform.enabled != 0;
        }
    }
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

float3 sample_radiance_map(
    TextureCube<float4> radiance_map,
    SamplerState environment_sampler,
    float3 direction,
    float lod
) {
    float3 radiance = radiance_map.SampleLevel(
        environment_sampler,
        direction,
        lod
    ).rgb;
    return is_finite_color(radiance) ?
        max(radiance, float3(0.0)) :
        float3(0.0);
}

public float3 sample_environment_radiance(
    EnvironmentMap environment,
    float3 world_direction,
    float perceptual_roughness
) {
    if (!environment.enabled) {
        return float3(0.0);
    }
    float3 direction = safe_normalize(
        mul(
            environment.uniform.environment_from_world,
            float4(world_direction, 0.0)
        ).xyz,
        float3(0.0, 1.0, 0.0)
    );
    return sample_radiance_map(
        environment.radiance_map,
        environment.cubemap_sampler,
        direction,
        saturate(perceptual_roughness) *
            environment.uniform.max_specular_lod
    );
}

MultiscatterTerms compute_multiscatter(
    float3 f0,
    float2 f_ab
) {
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
    EnvironmentMap environment,
    ShadingPoint point,
    StandardMaterial material
) {
    EnvironmentMapLighting lighting;
    lighting.diffuse = float3(0.0);
    lighting.specular = float3(0.0);
    if (!environment.enabled) {
        return lighting;
    }

    float3 normal = safe_normalize(
        mul(
            environment.uniform.environment_from_world,
            float4(point.normal_ws, 0.0)
        ).xyz,
        float3(0.0, 1.0, 0.0)
    );
    float3 reflection = safe_normalize(
        mul(
            environment.uniform.environment_from_world,
            float4(point.reflection_ws, 0.0)
        ).xyz,
        normal
    );
    float normal_dot_view = point.normal_dot_view;
    float3 irradiance =
        environment.irradiance_map.Sample(environment.cubemap_sampler, normal).rgb;
    float3 radiance_direction =
        radiance_sample_direction(normal, reflection, material.roughness);
    float3 radiance = sample_radiance_map(
        environment.radiance_map,
        environment.cubemap_sampler,
        radiance_direction,
        material.roughness * environment.uniform.max_specular_lod
    );
    if (!is_finite_color(irradiance)) {
        irradiance = float3(0.0);
    }
    irradiance = max(irradiance, float3(0.0));
    float2 brdf = environment.brdf_lut.Sample(
        environment.brdf_sampler,
        // The embedded LUT stores roughness from 1 at the top to 0 at the
        // bottom, while sampled texture coordinates increase downwards.
        float2(normal_dot_view, 1.0 - material.roughness)
    ).rg;
    if (!all(brdf == brdf) || any(abs(brdf) >= float2(1e20))) {
        brdf = float2(0.0);
    }
    MultiscatterTerms multiscatter =
        compute_multiscatter(material.f0, brdf);
    float3 specular_reflectance = saturate(
        multiscatter.single_scatter + multiscatter.multi_scatter
    );
    float3 diffuse_color =
        material.albedo *
        (float3(1.0) - specular_reflectance) *
        (1.0 - material.metallic);
    lighting.diffuse =
        environment.uniform.intensity * irradiance * diffuse_color;
    lighting.specular =
        environment.uniform.intensity *
        (radiance * multiscatter.single_scatter +
         irradiance * multiscatter.multi_scatter);
    if (!is_finite_color(lighting.specular)) {
        lighting.specular = float3(0.0);
    }
    lighting.specular = max(lighting.specular, float3(0.0));
    return lighting;
}

} // namespace pbr.environment_map
