import lib.constants;

namespace pbr.brdf {

using namespace pbr.constants;

public float distribution_ggx(float3 normal, float3 half_vector, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float normal_dot_half = max(dot(normal, half_vector), 0.0);
    float normal_dot_half2 = normal_dot_half * normal_dot_half;
    float denominator = normal_dot_half2 * (a2 - 1.0) + 1.0;
    denominator = PI * denominator * denominator;
    return a2 / max(denominator, EPSILON);
}

public float geometry_schlick_ggx(float normal_dot_view, float roughness) {
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return normal_dot_view /
        max(normal_dot_view * (1.0 - k) + k, EPSILON);
}

public float geometry_smith(float3 normal, float3 view, float3 light, float roughness) {
    float normal_dot_view = max(dot(normal, view), 0.0);
    float normal_dot_light = max(dot(normal, light), 0.0);
    return geometry_schlick_ggx(normal_dot_view, roughness) *
        geometry_schlick_ggx(normal_dot_light, roughness);
}

public float3 fresnel_schlick(float cos_theta, float3 f0) {
    return f0 + (1.0 - f0) * pow(1.0 - cos_theta, 5.0);
}

public float3 fresnel_schlick_roughness(float cos_theta, float3 f0, float roughness) {
    return f0 + (max(float3(1.0 - roughness), f0) - f0) *
        pow(clamp(1.0 - cos_theta, 0.0, 1.0), 5.0);
}

public struct BrdfSurface {
    public float3 normal;
    public float3 view;
    public float3 light;
    public float3 radiance;
    public float3 albedo;
    public float metallic;
    public float roughness;
    public float3 f0;

    public property float3 half_vector {
        get {
            return normalize(view + light);
        }
    }

    public property float normal_dot_light {
        get {
            return max(dot(normal, light), 0.0);
        }
    }

    public property float normal_dot_view {
        get {
            return max(dot(normal, view), 0.0);
        }
    }

    public property float3 fresnel {
        get {
            return fresnel_schlick(clamp(dot(half_vector, view), 0.0, 1.0), f0);
        }
    }

    public float3 evaluate_cook_torrance() {
        float n_dot_l = normal_dot_light;
        float n_dot_v = normal_dot_view;
        if (n_dot_l <= 0.0 || n_dot_v <= 0.0) {
            return float3(0.0);
        }

        float3 h = half_vector;
        float3 f = fresnel_schlick(clamp(dot(h, view), 0.0, 1.0), f0);
        float ndf = distribution_ggx(normal, h, roughness);
        float geometry = geometry_smith(normal, view, light, roughness);

        float3 numerator = ndf * geometry * f;
        float denominator = 4.0 * n_dot_v * n_dot_l + 0.001;
        float3 specular = numerator / denominator;

        float3 k_s = f;
        float3 k_d = (float3(1.0) - k_s) * (1.0 - metallic);
        float3 diffuse = k_d * albedo / PI;
        return (diffuse + specular) * radiance * n_dot_l;
    }
};

public float3 evaluate_cook_torrance(
    float3 normal,
    float3 view,
    float3 light,
    float3 radiance,
    float3 albedo,
    float metallic,
    float roughness,
    float3 f0
) {
    BrdfSurface surface;
    surface.normal = normal;
    surface.view = view;
    surface.light = light;
    surface.radiance = radiance;
    surface.albedo = albedo;
    surface.metallic = metallic;
    surface.roughness = roughness;
    surface.f0 = f0;
    return surface.evaluate_cook_torrance();
}

} // namespace pbr.brdf
