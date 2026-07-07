import lib.color;

namespace pbr.deferred {

using namespace pbr.color;

public struct GBufferSurface {
    public float3 position;
    public float3 normal;
    public float roughness;
    public float3 base_color;
    public float3 albedo;
    public float metallic;
    public float3 specular;
    public float specular_alpha;
    public float3 emissive;
    public float depth;

    public property float3 dielectric_f0 {
        get {
            return lerp(float3(0.04), albedo, metallic);
        }
    }

    public property float3 f0 {
        get {
            return lerp(dielectric_f0, clamp(specular, 0.0, 1.0), 0.5);
        }
    }

    public bool has_albedo() {
        return albedo.r > 0.0 || albedo.g > 0.0 || albedo.b > 0.0;
    }

    public bool has_specular() {
        return specular.r > 0.0 || specular.g > 0.0 || specular.b > 0.0;
    }
};

public float3 read_position(
    Texture2D<float4> g_position_ao,
    SamplerState g_buffer_sampler,
    float2 tex_coords
) {
    return g_position_ao.Sample(g_buffer_sampler, tex_coords).rgb;
}

public float3 read_normal(
    Texture2D<float4> g_normal_roughness,
    SamplerState g_buffer_sampler,
    float2 tex_coords
) {
    return normalize(g_normal_roughness.Sample(g_buffer_sampler, tex_coords).xyz);
}

public float read_roughness(
    Texture2D<float4> g_normal_roughness,
    SamplerState g_buffer_sampler,
    float2 tex_coords
) {
    return clamp(g_normal_roughness.Sample(g_buffer_sampler, tex_coords).a, 0.045, 1.0);
}

public float3 read_linear_albedo(
    Texture2D<float4> g_albedo_metallic,
    SamplerState g_buffer_sampler,
    float2 tex_coords
) {
    float3 base_color = g_albedo_metallic.Sample(g_buffer_sampler, tex_coords).rgb;
    return srgb_to_linear(base_color);
}

public float read_metallic(
    Texture2D<float4> g_albedo_metallic,
    SamplerState g_buffer_sampler,
    float2 tex_coords
) {
    return clamp(g_albedo_metallic.Sample(g_buffer_sampler, tex_coords).a, 0.0, 1.0);
}

public float3 read_specular(
    Texture2D<float4> g_specular,
    SamplerState g_buffer_sampler,
    float2 tex_coords
) {
    return g_specular.Sample(g_buffer_sampler, tex_coords).rgb;
}

public float3 read_emissive(
    Texture2D<float4> g_emissive_depth,
    SamplerState g_buffer_sampler,
    float2 tex_coords
) {
    return srgb_to_linear(
        g_emissive_depth.Sample(g_buffer_sampler, tex_coords).rgb
    );
}

public GBufferSurface read_lighting_surface(
    Texture2D<float4> g_position_ao,
    Texture2D<float4> g_normal_roughness,
    Texture2D<float4> g_albedo_metallic,
    Texture2D<float4> g_specular,
    SamplerState g_buffer_sampler,
    float2 tex_coords
) {
    float4 normal_roughness = g_normal_roughness.Sample(g_buffer_sampler, tex_coords);
    float4 albedo_metallic = g_albedo_metallic.Sample(g_buffer_sampler, tex_coords);

    GBufferSurface surface;
    surface.position = g_position_ao.Sample(g_buffer_sampler, tex_coords).rgb;
    surface.normal = normalize(normal_roughness.xyz);
    surface.roughness = clamp(normal_roughness.a, 0.045, 1.0);
    surface.base_color = albedo_metallic.rgb;
    surface.albedo = srgb_to_linear(surface.base_color);
    surface.metallic = clamp(albedo_metallic.a, 0.0, 1.0);
    float4 specular_value = g_specular.Sample(g_buffer_sampler, tex_coords);
    surface.specular = specular_value.rgb;
    surface.specular_alpha = specular_value.a;
    surface.emissive = float3(0.0);
    surface.depth = 0.0;
    return surface;
}

public GBufferSurface read_surface(
    Texture2D<float4> g_position_ao,
    Texture2D<float4> g_normal_roughness,
    Texture2D<float4> g_albedo_metallic,
    Texture2D<float4> g_specular,
    Texture2D<float4> g_emissive_depth,
    SamplerState g_buffer_sampler,
    float2 tex_coords
) {
    GBufferSurface surface = read_lighting_surface(
        g_position_ao,
        g_normal_roughness,
        g_albedo_metallic,
        g_specular,
        g_buffer_sampler,
        tex_coords
    );
    float4 emissive_depth = g_emissive_depth.Sample(g_buffer_sampler, tex_coords);
    surface.emissive = srgb_to_linear(emissive_depth.rgb);
    surface.depth = emissive_depth.a;
    return surface;
}

} // namespace pbr.deferred
