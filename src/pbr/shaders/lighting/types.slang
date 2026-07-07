public static const int MAX_DIRECTIONAL_LIGHTS = 3;
public static const int MAX_POINT_LIGHTS = 6;
public static const int MAX_SPOT_LIGHTS = 6;

public struct AttenuationData {
    public float constant;
    public float linear;
    public float quadratic;
};

public struct LightData {
    public float angle_inner_cone;
    public float angle_outer_cone;
    public float3 ambient;
    public float3 diffuse;
    public float3 specular;
    public float3 position;
    public float3 direction;
    public uint shadowing_method;
    public AttenuationData attenuation;
};

public struct LightingData {
    public LightData directional_lights[MAX_DIRECTIONAL_LIGHTS];
    public LightData point_lights[MAX_POINT_LIGHTS];
    public LightData spot_lights[MAX_SPOT_LIGHTS];
    public uint num_directional_lights;
    public uint num_point_lights;
    public uint num_spot_lights;
    public float4x4 light_view_projection;
};
