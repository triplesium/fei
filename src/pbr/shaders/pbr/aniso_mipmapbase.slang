import pbr.vxgi.aniso_mipmap;

using namespace pbr.vxgi.aniso_mipmap;

struct VxgiGenerateMipmapBaseUniform {
    int mip_dimension;
};

layout(set = 0, binding = 0) ConstantBuffer<VxgiGenerateMipmapBaseUniform> VxgiGenerateMipmapBase;
layout(set = 0, binding = 1) Texture3D<float4> voxel_base;
layout(set = 0, binding = 2, rgba8) RWTexture3D<float4> voxel_mipmap[6];

void fetch_texels(int3 position, inout float4 values[8]) {
    for (int i = 0; i < 8; i++) {
        values[i] = voxel_base.Load(int4(position + OFFSETS[i], 0));
    }
}

[shader("compute")]
[numthreads(8, 8, 8)]
void compute_main(uint3 dispatch_thread_id : SV_DispatchThreadID) {
    if (dispatch_thread_id.x >= VxgiGenerateMipmapBase.mip_dimension ||
        dispatch_thread_id.y >= VxgiGenerateMipmapBase.mip_dimension ||
        dispatch_thread_id.z >= VxgiGenerateMipmapBase.mip_dimension) {
        return;
    }

    int3 write_pos = int3(dispatch_thread_id);
    int3 source_pos = write_pos * 2;
    float4 values[8];
    float4 faces[6];
    fetch_texels(source_pos, values);
    compose_faces(values, faces);

    voxel_mipmap[0][write_pos] = faces[0];
    voxel_mipmap[1][write_pos] = faces[1];
    voxel_mipmap[2][write_pos] = faces[2];
    voxel_mipmap[3][write_pos] = faces[3];
    voxel_mipmap[4][write_pos] = faces[4];
    voxel_mipmap[5][write_pos] = faces[5];
}
