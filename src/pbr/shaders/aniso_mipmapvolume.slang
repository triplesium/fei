struct VxgiGenerateMipmapVolumeData {
    float3 mip_dimension;
    int mip_level;
};

layout(set = 0, binding = 0) ConstantBuffer<VxgiGenerateMipmapVolumeData> VxgiGenerateMipmapVolume;
layout(set = 0, binding = 1, rgba8) RWTexture3D<float4> voxel_mipmap_dst[6];
layout(set = 0, binding = 7) Texture3D<float4> voxel_mipmap_src[6];

static const int3 aniso_offsets[8] = {
    int3(1, 1, 1),
    int3(1, 1, 0),
    int3(1, 0, 1),
    int3(1, 0, 0),
    int3(0, 1, 1),
    int3(0, 1, 0),
    int3(0, 0, 1),
    int3(0, 0, 0),
};

void fetch_texels(int3 position, int direction, inout float4 values[8]) {
    for (int i = 0; i < 8; i++) {
        values[i] = voxel_mipmap_src[direction].Load(
            int4(position + aniso_offsets[i], VxgiGenerateMipmapVolume.mip_level)
        );
    }
}

[shader("compute")]
[numthreads(8, 8, 8)]
void compute_main(uint3 dispatch_thread_id : SV_DispatchThreadID) {
    if (dispatch_thread_id.x >= VxgiGenerateMipmapVolume.mip_dimension.x ||
        dispatch_thread_id.y >= VxgiGenerateMipmapVolume.mip_dimension.y ||
        dispatch_thread_id.z >= VxgiGenerateMipmapVolume.mip_dimension.z) {
        return;
    }

    int3 write_pos = int3(dispatch_thread_id);
    int3 source_pos = write_pos * 2;
    float4 values[8];

    fetch_texels(source_pos, 0, values);
    voxel_mipmap_dst[0][write_pos] =
        (values[0] + values[4] * (1.0 - values[0].a) +
         values[1] + values[5] * (1.0 - values[1].a) +
         values[2] + values[6] * (1.0 - values[2].a) +
         values[3] + values[7] * (1.0 - values[3].a)) *
        0.25;

    fetch_texels(source_pos, 1, values);
    voxel_mipmap_dst[1][write_pos] =
        (values[4] + values[0] * (1.0 - values[4].a) +
         values[5] + values[1] * (1.0 - values[5].a) +
         values[6] + values[2] * (1.0 - values[6].a) +
         values[7] + values[3] * (1.0 - values[7].a)) *
        0.25;

    fetch_texels(source_pos, 2, values);
    voxel_mipmap_dst[2][write_pos] =
        (values[0] + values[2] * (1.0 - values[0].a) +
         values[1] + values[3] * (1.0 - values[1].a) +
         values[5] + values[7] * (1.0 - values[5].a) +
         values[4] + values[6] * (1.0 - values[4].a)) *
        0.25;

    fetch_texels(source_pos, 3, values);
    voxel_mipmap_dst[3][write_pos] =
        (values[2] + values[0] * (1.0 - values[2].a) +
         values[3] + values[1] * (1.0 - values[3].a) +
         values[7] + values[5] * (1.0 - values[7].a) +
         values[6] + values[4] * (1.0 - values[6].a)) *
        0.25;

    fetch_texels(source_pos, 4, values);
    voxel_mipmap_dst[4][write_pos] =
        (values[0] + values[1] * (1.0 - values[0].a) +
         values[2] + values[3] * (1.0 - values[2].a) +
         values[4] + values[5] * (1.0 - values[4].a) +
         values[6] + values[7] * (1.0 - values[6].a)) *
        0.25;

    fetch_texels(source_pos, 5, values);
    voxel_mipmap_dst[5][write_pos] =
        (values[1] + values[0] * (1.0 - values[1].a) +
         values[3] + values[2] * (1.0 - values[3].a) +
         values[5] + values[4] * (1.0 - values[5].a) +
         values[7] + values[6] * (1.0 - values[7].a)) *
        0.25;
}
