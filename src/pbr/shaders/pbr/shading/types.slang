public struct ShadingPoint {
    public float3 position_ws;
    public float3 normal_ws;
    public float3 view_ws;

    public static ShadingPoint from_world(
        float3 position_ws,
        float3 normal_ws,
        float3 view_ws
    ) {
        ShadingPoint point;
        point.position_ws = position_ws;
        point.normal_ws = normal_ws;
        point.view_ws = view_ws;
        return point;
    }

    public property float normal_dot_view {
        get {
            return max(dot(normal_ws, view_ws), 0.0);
        }
    }

    public property float3 reflection_ws {
        get {
            return reflect(-view_ws, normal_ws);
        }
    }
};

public struct LightSample {
    public float3 direction_ws;
    public float3 radiance;

    public static LightSample from_radiance(
        float3 direction_ws,
        float3 radiance
    ) {
        LightSample sample;
        sample.direction_ws = direction_ws;
        sample.radiance = radiance;
        return sample;
    }
};
