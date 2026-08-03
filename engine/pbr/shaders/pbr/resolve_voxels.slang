import rendering.normal;
import pbr.vxgi.types;

using namespace rendering.normal;

layout(set = 0, binding = 0, rgba8) RWTexture3D<float4> voxel_albedo;
layout(set = 0, binding = 1, rgba8) RWTexture3D<float4> voxel_normal;
layout(set = 0, binding = 2, rgba8) RWTexture3D<float4> voxel_emissive;
layout(set = 0, binding = 4, r8) RWTexture3D<float4> static_voxel_flag;
layout(set = 1, binding = 0) ConstantBuffer<VxgiVoxelizationUniform> vxgi_voxelization;
layout(set = 2, binding = 0) StructuredBuffer<uint> voxel_albedo_accum;
layout(set = 2, binding = 1) StructuredBuffer<uint> voxel_normal_accum;
layout(set = 2, binding = 2) StructuredBuffer<uint> voxel_emissive_accum;
layout(set = 2, binding = 3) StructuredBuffer<uint> voxel_count_accum;

uint voxel_linear_index(uint3 position) {
    uint dimension = vxgi_voxelization.volume_dimension;
    return position.x + position.y * dimension + position.z * dimension * dimension;
}

float4 accumulated_color(StructuredBuffer<uint> buffer, uint offset, float count) {
    return float4(
        float(buffer[offset + 0]),
        float(buffer[offset + 1]),
        float(buffer[offset + 2]),
        float(buffer[offset + 3])
    ) / (255.0 * count);
}

float3 accumulated_normal(uint offset, float count) {
    float3 normal = float3(
        float(voxel_normal_accum[offset + 0]),
        float(voxel_normal_accum[offset + 1]),
        float(voxel_normal_accum[offset + 2])
    ) / (65535.0 * count);
    normal = normal * 2.0 - 1.0;
    if (dot(normal, normal) <= 0.000001) {
        return float3(0.0, 1.0, 0.0);
    }
    return normalize(normal);
}

[shader("compute")]
[numthreads(8, 8, 8)]
void compute_main(uint3 dispatch_thread_id : SV_DispatchThreadID) {
    if (!vxgi_voxelization.contains(dispatch_thread_id)) {
        return;
    }

    int3 position = int3(dispatch_thread_id);
    uint linear_index = voxel_linear_index(dispatch_thread_id);
    uint hit_count = voxel_count_accum[linear_index];
    if (hit_count == 0) {
        voxel_albedo[position] = float4(0.0);
        voxel_normal[position] = float4(0.0);
        voxel_emissive[position] = float4(0.0);
        static_voxel_flag[position] = float4(0.0);
        return;
    }

    float count = float(hit_count);
    uint color_offset = linear_index * 4;
    uint normal_offset = linear_index * 3;

    float4 albedo = accumulated_color(voxel_albedo_accum, color_offset, count);
    float3 normal = accumulated_normal(normal_offset, count);
    float4 emissive = accumulated_color(voxel_emissive_accum, color_offset, count);

    albedo.a = 1.0;
    emissive.a = 1.0;
    voxel_albedo[position] = saturate(albedo);
    voxel_normal[position] = float4(encode(normal), 1.0);
    voxel_emissive[position] = saturate(emissive);
    static_voxel_flag[position] =
        vxgi_voxelization.flag_static_voxels == 1 ? float4(1.0) : float4(0.0);
}
