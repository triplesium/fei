public struct VxgiUniform {
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
    public float skylight_leaking;

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
        enter = 0.0;
        leave = 1e20;
        for (int axis = 0; axis < 3; ++axis) {
            float direction = ray_direction[axis];
            if (abs(direction) < 1e-6) {
                if (ray_origin[axis] < world_min_point[axis] ||
                    ray_origin[axis] > world_max_point[axis]) {
                    return false;
                }
                continue;
            }

            float axis_enter =
                (world_min_point[axis] - ray_origin[axis]) / direction;
            float axis_leave =
                (world_max_point[axis] - ray_origin[axis]) / direction;
            if (axis_enter > axis_leave) {
                float temporary = axis_enter;
                axis_enter = axis_leave;
                axis_leave = temporary;
            }
            enter = max(enter, axis_enter);
            leave = min(leave, axis_leave);
            if (leave <= enter) {
                return false;
            }
        }
        return leave > 0.0;
    }
};

public struct VxgiVoxelizationUniform {
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
