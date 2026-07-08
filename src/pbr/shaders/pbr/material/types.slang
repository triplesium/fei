import rendering.normal;

using namespace rendering.normal;

public struct StandardMaterial {
    public float3 albedo;
    public float metallic;
    public float roughness;
    public float3 emissive;
    public float3 specular;
    public int flags;
};

public static const int STANDARD_MATERIAL_FLAG_ALBEDO_MAP = 1 << 0;
public static const int STANDARD_MATERIAL_FLAG_NORMAL_MAP = 1 << 1;
public static const int STANDARD_MATERIAL_FLAG_METALLIC_MAP = 1 << 2;
public static const int STANDARD_MATERIAL_FLAG_ROUGHNESS_MAP = 1 << 3;
public static const int STANDARD_MATERIAL_FLAG_EMISSIVE_MAP = 1 << 4;
public static const int STANDARD_MATERIAL_FLAG_SPECULAR_MAP = 1 << 5;

public struct StandardMaterialBlock {
    public StandardMaterial material;
    public Texture2D<float4> albedo_map;
    public Texture2D<float4> normal_map;
    public Texture2D<float4> metallic_map;
    public Texture2D<float4> roughness_map;
    public Texture2D<float4> emissive_map;
    public Texture2D<float4> specular_map;
    public SamplerState sampler;

    public property float3 albedo {
        get {
            return material.albedo;
        }
    }

    public property float metallic {
        get {
            return material.metallic;
        }
    }

    public property float roughness {
        get {
            return material.roughness;
        }
    }

    public property float3 emissive {
        get {
            return material.emissive;
        }
    }

    public property float3 specular {
        get {
            return material.specular;
        }
    }

    public property int flags {
        get {
            return material.flags;
        }
    }

    public bool has_flag(int flag) {
        return (flags & flag) != 0;
    }

    public property bool has_albedo_map {
        get {
            return has_flag(STANDARD_MATERIAL_FLAG_ALBEDO_MAP);
        }
    }

    public property bool has_normal_map {
        get {
            return has_flag(STANDARD_MATERIAL_FLAG_NORMAL_MAP);
        }
    }

    public property bool has_metallic_map {
        get {
            return has_flag(STANDARD_MATERIAL_FLAG_METALLIC_MAP);
        }
    }

    public property bool has_roughness_map {
        get {
            return has_flag(STANDARD_MATERIAL_FLAG_ROUGHNESS_MAP);
        }
    }

    public property bool has_emissive_map {
        get {
            return has_flag(STANDARD_MATERIAL_FLAG_EMISSIVE_MAP);
        }
    }

    public property bool has_specular_map {
        get {
            return has_flag(STANDARD_MATERIAL_FLAG_SPECULAR_MAP);
        }
    }

    public float4 sample_albedo_texture(float2 tex_coords) {
        return albedo_map.Sample(sampler, tex_coords);
    }

    public float4 sample_emissive_texture(float2 tex_coords) {
        return emissive_map.Sample(sampler, tex_coords);
    }

    public float3 sample_tangent_normal(float2 tex_coords) {
        return normal_map.Sample(sampler, tex_coords).xyz * 2.0 - 1.0;
    }

    public float3 sample_albedo(float2 tex_coords) {
#if defined(VERTEX_UVS)
        if (has_albedo_map) {
            return sample_albedo_texture(tex_coords).rgb;
        }
#endif
        return albedo;
    }

    public float sample_metallic(float2 tex_coords) {
#if defined(VERTEX_UVS)
        if (has_metallic_map) {
            return metallic_map.Sample(sampler, tex_coords).r;
        }
#endif
        return metallic;
    }

    public float sample_roughness(float2 tex_coords) {
#if defined(VERTEX_UVS)
        if (has_roughness_map) {
            return roughness_map.Sample(sampler, tex_coords).r;
        }
#endif
        return roughness;
    }

    public float3 sample_emissive(float2 tex_coords) {
#if defined(VERTEX_UVS)
        if (has_emissive_map) {
            return sample_emissive_texture(tex_coords).rgb;
        }
#endif
        return emissive;
    }

    public float3 sample_specular(float2 tex_coords) {
#if defined(VERTEX_UVS)
        if (has_specular_map) {
            return specular_map.Sample(sampler, tex_coords).rgb;
        }
#endif
        return specular;
    }

    public float3 sample_normal_from_derivatives(
        float2 tex_coords,
        float3 position_ws,
        float3 normal_ws
    ) {
#if defined(VERTEX_UVS)
        if (has_normal_map) {
            return from_derivatives(
                sample_tangent_normal(tex_coords),
                position_ws,
                normal_ws,
                tex_coords
            );
        }
#endif
        return normalize(normal_ws);
    }

    public float3 sample_normal_from_tangent_frame(
        float2 tex_coords,
        float3 normal_ws,
        float3 tangent_ws
    ) {
#if defined(VERTEX_UVS) && defined(VERTEX_TANGENTS)
        if (has_normal_map) {
            return from_tangent_frame(
                sample_tangent_normal(tex_coords),
                normal_ws,
                tangent_ws
            );
        }
#endif
        return normalize(normal_ws);
    }
};
