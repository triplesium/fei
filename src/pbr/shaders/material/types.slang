public struct SimpleMaterialData {
    public float3 albedo;
    public float metallic;
    public float roughness;
    public int flags;
};

public struct PbrMaterialData {
    public float3 albedo;
    public float metallic;
    public float roughness;
    public float3 emissive;
    public float3 specular;
    public int flags;
};
