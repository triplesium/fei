public struct VxgiData {
    public float voxel_scale;
    public float3 world_min_point;
    public float3 world_max_point;
    public int volume_dimension;
    public float max_tracing_distance_global;
    public float bounce_strength;
    public float ao_falloff;
    public float ao_alpha;
    public float sampling_factor;
    public uint mode;

    public property float inverse_voxel_scale {
        get {
            return 1.0 / voxel_scale;
        }
    }

    public property float cone_trace_voxel_world_size {
        get {
            return 2.0 / (voxel_scale * volume_dimension);
        }
    }

    public float3 world_to_voxel(float3 position_ws) {
        return (position_ws - world_min_point) * voxel_scale;
    }

    public bool intersect_world_aabb(
        float3 ray_origin,
        float3 ray_direction,
        out float enter,
        out float leave
    ) {
        float3 temp_min = (world_min_point - ray_origin) / ray_direction;
        float3 temp_max = (world_max_point - ray_origin) / ray_direction;
        float3 v3_max = max(temp_max, temp_min);
        float3 v3_min = min(temp_max, temp_min);
        leave = min(v3_max.x, min(v3_max.y, v3_max.z));
        enter = max(max(v3_min.x, 0.0), max(v3_min.y, v3_min.z));
        return leave > enter;
    }
};

public struct VxgiVoxelizationData {
    public float4x4 view_projections[3];
    public float4x4 inv_view_projections[3];
    public uint volume_dimension;
    public uint flag_static_voxels;
    public float voxel_scale;
    public float voxel_size;
    public float3 world_min_point;

    public property float voxel_texel_size {
        get {
            return 1.0 / volume_dimension;
        }
    }

    public property float voxel_world_size {
        get {
            return 1.0 / (voxel_scale * volume_dimension);
        }
    }

    public property float2 half_pixel {
        get {
            return float2(voxel_texel_size);
        }
    }

    public float4x4 view_projection(int axis) {
        return view_projections[axis];
    }

    public bool contains(uint3 position) {
        return position.x < volume_dimension &&
            position.y < volume_dimension &&
            position.z < volume_dimension;
    }

    public float3 world_to_voxel(float3 position_ws) {
        return (position_ws - world_min_point) * voxel_scale;
    }

    public float3 world_to_voxel_index(float3 position_ws) {
        return world_to_voxel(position_ws) * volume_dimension;
    }

    public int3 voxel_index(float3 voxel_position) {
        return int3(voxel_position * float(volume_dimension));
    }

    public float3 voxel_to_world(int3 position) {
        return float3(position) * voxel_size + world_min_point;
    }
};
