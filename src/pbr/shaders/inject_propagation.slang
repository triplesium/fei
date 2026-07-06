struct VxgiInjectPropagationData {
    float max_tracing_distance_global;
    int volume_dimension;
    uint check_boundaries;
};

layout(set = 0, binding = 0) ConstantBuffer<VxgiInjectPropagationData> VxgiInjectPropagation;
layout(set = 0, binding = 1, rgba8) RWTexture3D<float4> voxel_composite;
layout(set = 0, binding = 2) Texture3D<float4> voxel_albedo;
layout(set = 0, binding = 3) Texture3D<float4> voxel_normal;
layout(set = 0, binding = 4) Texture3D<float4> voxel_tex_mipmap[6];
layout(set = 0, binding = 10) SamplerState voxel_sampler;

static const float PI = 3.14159265;
static const float EPSILON = 1e-30;

static const float3 propagation_directions[4] = {
    float3(0.0, 1.0, 0.0),
    float3(0.0, 0.5, 0.866025),
    float3(0.754996, 0.5, -0.4330128),
    float3(-0.754996, 0.5, -0.4330128),
};

static const float diffuse_cone_weights[4] = {
    PI / 3.0,
    2.0 * PI / 9.0,
    2.0 * PI / 9.0,
    2.0 * PI / 9.0,
};

float4 trace_cone(float3 position, float3 direction, float aperture) {
    uint3 visible_face;
    float aniso_dimension = VxgiInjectPropagation.volume_dimension / 2.0;
    visible_face.x = direction.x < 0.0 ? 0 : 1;
    visible_face.y = direction.y < 0.0 ? 2 : 3;
    visible_face.z = direction.z < 0.0 ? 4 : 5;
    float3 weight = direction * direction;
    float voxel_size = 1.0 / aniso_dimension;
    float dst = voxel_size;
    float diameter = aperture * dst;
    float3 sample_position = position + direction * dst;
    float mip_level = 0.0;
    float4 cone_sample = float4(0.0);

    if (sample_position.x < 0.0 ||
        sample_position.y < 0.0 ||
        sample_position.z < 0.0 ||
        sample_position.x > 1.0 ||
        sample_position.y > 1.0 ||
        sample_position.z > 1.0) {
        return cone_sample;
    }

    while (cone_sample.a <= 1.0 &&
           dst <= VxgiInjectPropagation.max_tracing_distance_global) {
        if (VxgiInjectPropagation.check_boundaries > 0 &&
            (sample_position.x < 0.0 ||
             sample_position.y < 0.0 ||
             sample_position.z < 0.0 ||
             sample_position.x > 1.0 ||
             sample_position.y > 1.0 ||
             sample_position.z > 1.0)) {
            break;
        }

        mip_level = log2(diameter * aniso_dimension);
        mip_level = max(mip_level - 1.0, 0.0);
        float4 aniso_sample =
            weight.x * voxel_tex_mipmap[visible_face.x].SampleLevel(
                voxel_sampler,
                sample_position,
                mip_level
            ) +
            weight.y * voxel_tex_mipmap[visible_face.y].SampleLevel(
                voxel_sampler,
                sample_position,
                mip_level
            ) +
            weight.z * voxel_tex_mipmap[visible_face.z].SampleLevel(
                voxel_sampler,
                sample_position,
                mip_level
            );
        cone_sample += (1.0 - cone_sample.a) * aniso_sample;
        dst += max(diameter, voxel_size);
        diameter = dst * aperture;
        sample_position = direction * dst + position;
    }

    return cone_sample;
}

float4 calculate_indirect_lighting(float3 position, float3 normal) {
    position = position + normal * (1.0 / (VxgiInjectPropagation.volume_dimension / 2.0));
    float4 diffuse_trace = float4(0.0);
    const float aperture = 1.0;
    float3 guide = float3(0.0, 1.0, 0.0);

    if (abs(dot(normal, guide)) == 1.0) {
        guide = float3(0.0, 0.0, 1.0);
    }

    float3 right = normalize(guide - dot(normal, guide) * normal);
    float3 up = cross(right, normal);

    for (int i = 0; i < 4; i++) {
        float3 cone_direction = normal;
        cone_direction += propagation_directions[i].x * right +
            propagation_directions[i].z * up;
        cone_direction = normalize(cone_direction);
        diffuse_trace +=
            trace_cone(position, cone_direction, aperture) * diffuse_cone_weights[i];
    }

    return clamp(diffuse_trace, 0.0, 1.0);
}

float3 decode_normal(float3 normal) {
    return normal * 2.0 - float3(1.0);
}

[shader("compute")]
[numthreads(8, 8, 8)]
void compute_main(uint3 dispatch_thread_id : SV_DispatchThreadID) {
    if (dispatch_thread_id.x >= VxgiInjectPropagation.volume_dimension ||
        dispatch_thread_id.y >= VxgiInjectPropagation.volume_dimension ||
        dispatch_thread_id.z >= VxgiInjectPropagation.volume_dimension) {
        return;
    }

    int3 write_pos = int3(dispatch_thread_id);
    float4 albedo = voxel_albedo.Load(int4(write_pos, 0));
    if (albedo.a < EPSILON) {
        return;
    }

    float4 direct_light = voxel_composite[write_pos];
    float3 normal = voxel_normal.Load(int4(write_pos, 0)).xyz;
    normal = normalize(decode_normal(normal));
    float3 position = float3(write_pos) / VxgiInjectPropagation.volume_dimension;
    float4 indirect_lighting = calculate_indirect_lighting(position, normal);
    indirect_lighting *= albedo;

    float4 radiance = direct_light + indirect_lighting;
    radiance.a = direct_light.a;
    voxel_composite[write_pos] = radiance;
}
