import pbr.vxgi.types;

layout(set = 0, binding = 0, rgba8) RWTexture3D<float4> voxel_albedo;
layout(set = 0, binding = 1, rgba8) RWTexture3D<float4> voxel_normal;
layout(set = 0, binding = 2, rgba8) RWTexture3D<float4> voxel_emissive;
layout(set = 0, binding = 3, rgba8) RWTexture3D<float4> voxel_radiance;
layout(set = 0, binding = 4, r8) RWTexture3D<float4> static_voxel_flag;
layout(set = 1, binding = 0) ConstantBuffer<VxgiVoxelizationUniform> VxgiVoxelization;
layout(set = 2, binding = 0) RWStructuredBuffer<uint> voxel_albedo_accum;
layout(set = 2, binding = 1) RWStructuredBuffer<uint> voxel_normal_accum;
layout(set = 2, binding = 2) RWStructuredBuffer<uint> voxel_emissive_accum;
layout(set = 2, binding = 3) RWStructuredBuffer<uint> voxel_count_accum;

[shader("compute")]
[numthreads(8, 8, 8)]
void compute_main(uint3 dispatch_thread_id : SV_DispatchThreadID) {
    if (!VxgiVoxelization.contains(dispatch_thread_id)) {
        return;
    }

    int3 position = int3(dispatch_thread_id);
    voxel_albedo[position] = float4(0.0);
    voxel_normal[position] = float4(0.0);
    voxel_emissive[position] = float4(0.0);
    voxel_radiance[position] = float4(0.0);
    static_voxel_flag[position] = float4(0.0);

    uint dimension = VxgiVoxelization.volume_dimension;
    uint linear_index = dispatch_thread_id.x +
        dispatch_thread_id.y * dimension +
        dispatch_thread_id.z * dimension * dimension;
    uint color_offset = linear_index * 4;
    uint normal_offset = linear_index * 3;

    voxel_albedo_accum[color_offset + 0] = 0;
    voxel_albedo_accum[color_offset + 1] = 0;
    voxel_albedo_accum[color_offset + 2] = 0;
    voxel_albedo_accum[color_offset + 3] = 0;
    voxel_normal_accum[normal_offset + 0] = 0;
    voxel_normal_accum[normal_offset + 1] = 0;
    voxel_normal_accum[normal_offset + 2] = 0;
    voxel_emissive_accum[color_offset + 0] = 0;
    voxel_emissive_accum[color_offset + 1] = 0;
    voxel_emissive_accum[color_offset + 2] = 0;
    voxel_emissive_accum[color_offset + 3] = 0;
    voxel_count_accum[linear_index] = 0;
}
