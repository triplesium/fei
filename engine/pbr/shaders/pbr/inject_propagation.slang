import rendering.constants;
import rendering.normal;
import pbr.vxgi.basis;

using namespace rendering.constants;
using namespace rendering.normal;
using namespace pbr.vxgi;

struct VxgiInjectPropagationUniform {
    float max_tracing_distance_global;
    int volume_dimension;
    uint check_boundaries;

    property float aniso_dimension {
        get {
            return volume_dimension / 2.0;
        }
    }

    property float voxel_size {
        get {
            return 1.0 / aniso_dimension;
        }
    }

    bool contains(uint3 position) {
        return position.x < volume_dimension &&
            position.y < volume_dimension &&
            position.z < volume_dimension;
    }

    bool outside_unit_volume(float3 position) {
        return position.x < 0.0 ||
            position.y < 0.0 ||
            position.z < 0.0 ||
            position.x > 1.0 ||
            position.y > 1.0 ||
            position.z > 1.0;
    }

    float3 voxel_position(int3 position) {
        return float3(position) / volume_dimension;
    }
};

layout(set = 0, binding = 0) ConstantBuffer<VxgiInjectPropagationUniform> vxgi_inject_propagation;
layout(set = 0, binding = 1, rgba8) RWTexture3D<float4> voxel_composite;
layout(set = 0, binding = 2) Texture3D<float4> voxel_albedo;
layout(set = 0, binding = 3) Texture3D<float4> voxel_normal;
layout(set = 0, binding = 4) Texture3D<float4> voxel_tex_mipmap[6];
layout(set = 0, binding = 10) SamplerState voxel_sampler;

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
    float aniso_dimension = vxgi_inject_propagation.aniso_dimension;
    uint3 visible_face = visible_aniso_faces(direction);
    float3 weight = direction * direction;
    float voxel_size = vxgi_inject_propagation.voxel_size;
    float dst = voxel_size;
    float diameter = aperture * dst;
    float3 sample_position = position + direction * dst;
    float mip_level = 0.0;
    float4 cone_sample = float4(0.0);

    if (vxgi_inject_propagation.outside_unit_volume(sample_position)) {
        return cone_sample;
    }

    while (cone_sample.a <= 1.0 &&
           dst <= vxgi_inject_propagation.max_tracing_distance_global) {
        if (vxgi_inject_propagation.check_boundaries > 0 &&
            vxgi_inject_propagation.outside_unit_volume(sample_position)) {
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
    position = position + normal * vxgi_inject_propagation.voxel_size;
    float4 diffuse_trace = float4(0.0);
    const float aperture = 1.0;
    OrthonormalBasis basis = OrthonormalBasis.from_normal(normal);

    for (int i = 0; i < 4; i++) {
        float3 cone_direction = basis.cone_direction(normal, propagation_directions[i]);
        diffuse_trace +=
            trace_cone(position, cone_direction, aperture) * diffuse_cone_weights[i];
    }

    return clamp(diffuse_trace, 0.0, 1.0);
}

[shader("compute")]
[numthreads(8, 8, 8)]
void compute_main(uint3 dispatch_thread_id : SV_DispatchThreadID) {
    if (!vxgi_inject_propagation.contains(dispatch_thread_id)) {
        return;
    }

    int3 write_pos = int3(dispatch_thread_id);
    float4 albedo = voxel_albedo.Load(int4(write_pos, 0));
    if (albedo.a < EPSILON) {
        return;
    }

    float4 direct_light = voxel_composite[write_pos];
    float3 normal = voxel_normal.Load(int4(write_pos, 0)).xyz;
    normal = normalize(decode(normal));
    float3 position = vxgi_inject_propagation.voxel_position(write_pos);
    float4 indirect_lighting = calculate_indirect_lighting(position, normal);
    indirect_lighting *= albedo;

    float4 radiance = direct_light + indirect_lighting;
    radiance.a = direct_light.a;
    voxel_composite[write_pos] = radiance;
}
