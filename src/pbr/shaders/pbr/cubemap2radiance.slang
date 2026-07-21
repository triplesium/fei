import pbr.lib.brdf;
import rendering.constants;
import pbr.lib.cubemap;

using namespace pbr.brdf;
using namespace rendering.constants;
using namespace pbr.ibl;

layout(set = 0, binding = 0) TextureCube<float4> cubemap;
layout(set = 0, binding = 1) SamplerState cubemap_sampler;
layout(set = 0, binding = 2, rgba32f) RWTexture2DArray<float4> output_texture;

struct FilteringConstants {
    float roughness;
};

layout(set = 0, binding = 3) ConstantBuffer<FilteringConstants> constants;

static const uint SAMPLE_COUNT = 1024u;

bool is_finite_color(float3 value) {
    return all(value == value) && all(abs(value) < float3(1e20));
}

[shader("compute")]
[numthreads(8, 8, 1)]
void compute_main(uint3 dispatch_thread_id : SV_DispatchThreadID) {
    uint width;
    uint height;
    uint layers;
    output_texture.GetDimensions(width, height, layers);
    if (dispatch_thread_id.x >= width ||
        dispatch_thread_id.y >= height ||
        dispatch_thread_id.z >= layers) {
        return;
    }

    float3 normal = cube_face_sampling_vector(dispatch_thread_id, uint2(width, height));
    float3 reflection = normal;
    float3 view = reflection;

    if (constants.roughness < 0.01) {
        float3 radiance =
            cubemap.SampleLevel(cubemap_sampler, reflection, 0.0).rgb;
        if (!is_finite_color(radiance)) {
            radiance = float3(0.0);
        }
        radiance = max(radiance, float3(0.0));
        output_texture[dispatch_thread_id] = float4(radiance, 1.0);
        return;
    }

    float3 color = float3(0.0);
    float total_weight = 0.0;
    for (uint i = 0u; i < SAMPLE_COUNT; i++) {
        float2 xi = hammersley(i, SAMPLE_COUNT);
        float3 half_vector = importance_sample_ggx(xi, normal, constants.roughness);
        float3 light = normalize(2.0 * dot(view, half_vector) * half_vector - view);
        float n_dot_l = max(dot(normal, light), 0.0);
        if (n_dot_l > 0.0) {
            float d = distribution_ggx(normal, half_vector, constants.roughness);
            float n_dot_h = max(dot(normal, half_vector), 0.0);
            float h_dot_v = max(dot(half_vector, view), 0.0);
            float pdf = d * n_dot_h / (4.0 * h_dot_v) + 0.00001;
            float mip_level = calculate_env_map_lod(pdf, 1024.0, SAMPLE_COUNT);
            float3 sample_color =
                cubemap.SampleLevel(cubemap_sampler, light, mip_level).rgb;
            if (is_finite_color(sample_color)) {
                sample_color = max(sample_color, float3(0.0));
                color += sample_color * n_dot_l;
                total_weight += n_dot_l;
            }
        }
    }

    if (total_weight > 1e-5) {
        color /= total_weight;
    } else {
        color = cubemap.SampleLevel(
            cubemap_sampler,
            reflection,
            0.0
        ).rgb;
    }
    if (!is_finite_color(color)) {
        color = float3(0.0);
    }
    color = max(color, float3(0.0));
    output_texture[dispatch_thread_id] = float4(color, 1.0);
}
