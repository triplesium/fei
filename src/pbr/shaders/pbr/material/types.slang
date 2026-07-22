import rendering.normal;

using namespace rendering.normal;

public struct StandardMaterialUniform {
    public float3 albedo;
    public float metallic;
    public float roughness;
    public float3 emissive;
    public float3 specular;
    public int flags;
    public float normal_scale;
    public float occlusion_strength;
    public int albedo_channel;
    public int normal_channel;
    public int metallic_roughness_channel;
    public int occlusion_channel;
    public int emissive_channel;
    public int specular_channel;
};

public struct StandardMaterialTexCoords {
    public float2 uv0;
    public float2 uv1;

    public float2 get(int channel) {
        return channel == 1 ? uv1 : uv0;
    }

    public bool has(int channel) {
        if (channel == 1) {
#if defined(VERTEX_UVS_1)
            return true;
#else
            return false;
#endif
        }
#if defined(VERTEX_UVS)
        return true;
#else
        return false;
#endif
    }
};

public static const int STANDARD_MATERIAL_FLAG_ALBEDO_MAP = 1 << 0;
public static const int STANDARD_MATERIAL_FLAG_NORMAL_MAP = 1 << 1;
public static const int STANDARD_MATERIAL_FLAG_METALLIC_ROUGHNESS_MAP = 1 << 2;
public static const int STANDARD_MATERIAL_FLAG_OCCLUSION_MAP = 1 << 3;
public static const int STANDARD_MATERIAL_FLAG_EMISSIVE_MAP = 1 << 4;
public static const int STANDARD_MATERIAL_FLAG_SPECULAR_MAP = 1 << 5;

public struct StandardMaterial {
    public float3 albedo;
    public float metallic;
    public float roughness;
    public float3 emissive;
    public float3 specular;
    public float3 f0;

    public static StandardMaterial from_values(
        float3 albedo,
        float metallic,
        float roughness,
        float3 emissive,
        float3 specular
    ) {
        return StandardMaterial.from_f0(
            albedo,
            metallic,
            roughness,
            emissive,
            specular,
            lerp(float3(0.04), albedo, metallic)
        );
    }

    public static StandardMaterial from_f0(
        float3 albedo,
        float metallic,
        float roughness,
        float3 emissive,
        float3 specular,
        float3 f0
    ) {
        StandardMaterial result;
        result.albedo = albedo;
        result.metallic = metallic;
        result.roughness = roughness;
        result.emissive = emissive;
        result.specular = specular;
        result.f0 = f0;
        return result;
    }
};

public struct StandardMaterialResources {
    public StandardMaterialUniform material;
    public Texture2D<float4> albedo_map;
    public SamplerState albedo_sampler;
    public Texture2D<float4> normal_map;
    public SamplerState normal_sampler;
    public Texture2D<float4> metallic_roughness_map;
    public SamplerState metallic_roughness_sampler;
    public Texture2D<float4> occlusion_map;
    public SamplerState occlusion_sampler;
    public Texture2D<float4> emissive_map;
    public SamplerState emissive_sampler;
    public Texture2D<float4> specular_map;
    public SamplerState specular_sampler;

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

    public property bool has_metallic_roughness_map {
        get {
            return has_flag(STANDARD_MATERIAL_FLAG_METALLIC_ROUGHNESS_MAP);
        }
    }

    public property bool has_occlusion_map {
        get {
            return has_flag(STANDARD_MATERIAL_FLAG_OCCLUSION_MAP);
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

    public float4 sample_albedo_texture(StandardMaterialTexCoords tex_coords) {
        return albedo_map.Sample(
            albedo_sampler,
            tex_coords.get(material.albedo_channel)
        );
    }

    public float4 sample_emissive_texture(StandardMaterialTexCoords tex_coords) {
        return emissive_map.Sample(
            emissive_sampler,
            tex_coords.get(material.emissive_channel)
        );
    }

    public float3 sample_tangent_normal(StandardMaterialTexCoords tex_coords) {
        float3 normal = normal_map
                            .Sample(
                                normal_sampler,
                                tex_coords.get(material.normal_channel)
                            )
                            .xyz *
            2.0 - 1.0;
        normal.xy *= material.normal_scale;
        return normal;
    }

    public float3 sample_albedo(StandardMaterialTexCoords tex_coords) {
#if defined(VERTEX_UVS) || defined(VERTEX_UVS_1)
        if (has_albedo_map && tex_coords.has(material.albedo_channel)) {
            return albedo * sample_albedo_texture(tex_coords).rgb;
        }
#endif
        return albedo;
    }

    public float sample_metallic(StandardMaterialTexCoords tex_coords) {
#if defined(VERTEX_UVS) || defined(VERTEX_UVS_1)
        if (has_metallic_roughness_map &&
            tex_coords.has(material.metallic_roughness_channel)) {
            return metallic * metallic_roughness_map
                .Sample(
                    metallic_roughness_sampler,
                    tex_coords.get(material.metallic_roughness_channel)
                )
                .b;
        }
#endif
        return metallic;
    }

    public float sample_roughness(StandardMaterialTexCoords tex_coords) {
#if defined(VERTEX_UVS) || defined(VERTEX_UVS_1)
        if (has_metallic_roughness_map &&
            tex_coords.has(material.metallic_roughness_channel)) {
            return roughness * metallic_roughness_map
                .Sample(
                    metallic_roughness_sampler,
                    tex_coords.get(material.metallic_roughness_channel)
                )
                .g;
        }
#endif
        return roughness;
    }

    public float3 sample_emissive(StandardMaterialTexCoords tex_coords) {
#if defined(VERTEX_UVS) || defined(VERTEX_UVS_1)
        if (has_emissive_map && tex_coords.has(material.emissive_channel)) {
            return emissive * sample_emissive_texture(tex_coords).rgb;
        }
#endif
        return emissive;
    }

    public float3 sample_specular(StandardMaterialTexCoords tex_coords) {
#if defined(VERTEX_UVS) || defined(VERTEX_UVS_1)
        if (has_specular_map && tex_coords.has(material.specular_channel)) {
            return specular * specular_map
                                  .Sample(
                                      specular_sampler,
                                      tex_coords.get(material.specular_channel)
                                  )
                                  .rgb;
        }
#endif
        return specular;
    }

    public float sample_occlusion(StandardMaterialTexCoords tex_coords) {
#if defined(VERTEX_UVS) || defined(VERTEX_UVS_1)
        if (has_occlusion_map && tex_coords.has(material.occlusion_channel)) {
            float occlusion = occlusion_map
                                  .Sample(
                                      occlusion_sampler,
                                      tex_coords.get(material.occlusion_channel)
                                  )
                                  .r;
            return lerp(1.0, occlusion, material.occlusion_strength);
        }
#endif
        return 1.0;
    }

    public StandardMaterial sample(StandardMaterialTexCoords tex_coords) {
        return StandardMaterial.from_values(
            sample_albedo(tex_coords),
            sample_metallic(tex_coords),
            clamp(sample_roughness(tex_coords), 0.01, 1.0),
            sample_emissive(tex_coords),
            sample_specular(tex_coords)
        );
    }

    public float3 sample_normal_from_derivatives(
        StandardMaterialTexCoords tex_coords,
        float3 position_ws,
        float3 normal_ws
    ) {
#if defined(VERTEX_UVS) || defined(VERTEX_UVS_1)
        if (has_normal_map && tex_coords.has(material.normal_channel)) {
            float2 normal_tex_coords =
                tex_coords.get(material.normal_channel);
            return from_derivatives(
                sample_tangent_normal(tex_coords),
                position_ws,
                normal_ws,
                normal_tex_coords
            );
        }
#endif
        return normalize(normal_ws);
    }

    public float3 sample_normal_from_tangent_frame(
        StandardMaterialTexCoords tex_coords,
        float3 normal_ws,
        float3 tangent_ws
    ) {
#if (defined(VERTEX_UVS) || defined(VERTEX_UVS_1)) && defined(VERTEX_TANGENTS)
        if (has_normal_map && tex_coords.has(material.normal_channel)) {
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
