public struct ViewData {
    public float4x4 clip_from_world;
    public float4x4 view_from_world;
    public float4x4 clip_from_view;
    public float3 world_position;

    public float4 position_cs(float3 position_ws) {
        return mul(clip_from_world, float4(position_ws, 1.0));
    }

    public float4 position_cs(float4 position_ws) {
        return mul(clip_from_world, position_ws);
    }

    public float3 view_direction(float3 position_ws) {
        return normalize(world_position - position_ws);
    }
};
