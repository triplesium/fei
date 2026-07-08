import pbr.vxgi.aniso_mipmap;

using namespace pbr.vxgi.aniso_mipmap;

struct VxgiGenerateMipmapVolumeUniform {
    float3 mip_dimension;
    int mip_level;
};

layout(set = 0, binding = 0) ConstantBuffer<VxgiGenerateMipmapVolumeUniform> VxgiGenerateMipmapVolume;
layout(set = 0, binding = 1, rgba8) RWTexture3D<float4> voxel_mipmap_dst[6];
layout(set = 0, binding = 7) Texture3D<float4> voxel_mipmap_src[6];

void fetch_texels(int3 position, int direction, inout float4 values[8]) {
    for (int i = 0; i < 8; i++) {
        values[i] = voxel_mipmap_src[direction].Load(
            int4(position + OFFSETS[i], VxgiGenerateMipmapVolume.mip_level)
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
    float4 faces[6];

    fetch_texels(source_pos, 0, values);
    compose_faces(values, faces);
    voxel_mipmap_dst[0][write_pos] = faces[0];

    fetch_texels(source_pos, 1, values);
    compose_faces(values, faces);
    voxel_mipmap_dst[1][write_pos] = faces[1];

    fetch_texels(source_pos, 2, values);
    compose_faces(values, faces);
    voxel_mipmap_dst[2][write_pos] = faces[2];

    fetch_texels(source_pos, 3, values);
    compose_faces(values, faces);
    voxel_mipmap_dst[3][write_pos] = faces[3];

    fetch_texels(source_pos, 4, values);
    compose_faces(values, faces);
    voxel_mipmap_dst[4][write_pos] = faces[4];

    fetch_texels(source_pos, 5, values);
    compose_faces(values, faces);
    voxel_mipmap_dst[5][write_pos] = faces[5];
}
