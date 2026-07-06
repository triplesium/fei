layout(set = 0, binding = 0) TextureCube<float4> cubemap;
layout(set = 0, binding = 1) SamplerState cubemap_sampler;
layout(set = 0, binding = 2, rgba32f) RWTexture2DArray<float4> output_texture;

struct FilteringConstantsData {
    float roughness;
};

layout(set = 0, binding = 3) ConstantBuffer<FilteringConstantsData> Constants;

static const float PI = 3.14159265359;
static const float TWO_PI = 2.0 * PI;
static const float EPSILON = 0.00001;
static const uint SAMPLE_COUNT = 1024u;

float3 get_sampling_vector(uint3 dispatch_thread_id) {
    uint width;
    uint height;
    uint layers;
    output_texture.GetDimensions(width, height, layers);
    float2 st = float2(dispatch_thread_id.xy) / float2(width, height);
    float2 uv = 2.0 * float2(st.x, 1.0 - st.y) - float2(1.0);

    float3 result = float3(0.0);
    switch (dispatch_thread_id.z) {
    case 0:
        result = float3(1.0, uv.y, -uv.x);
        break;
    case 1:
        result = float3(-1.0, uv.y, uv.x);
        break;
    case 2:
        result = float3(uv.x, 1.0, -uv.y);
        break;
    case 3:
        result = float3(uv.x, -1.0, uv.y);
        break;
    case 4:
        result = float3(uv.x, uv.y, 1.0);
        break;
    default:
        result = float3(-uv.x, uv.y, -1.0);
        break;
    }
    return normalize(result);
}

float ndf_ggx(float3 normal, float3 half_vector, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float n_dot_h = max(dot(normal, half_vector), 0.0);
    float n_dot_h2 = n_dot_h * n_dot_h;
    float denominator = n_dot_h2 * (a2 - 1.0) + 1.0;
    denominator = PI * denominator * denominator;
    return a2 / denominator;
}

float radical_inverse_vdc(uint bits) {
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10;
}

float2 hammersley(uint index, uint count) {
    return float2(float(index) / float(count), radical_inverse_vdc(index));
}

float3 importance_sample_ggx(float2 xi, float3 normal, float roughness) {
    float a = roughness * roughness;
    float phi = 2.0 * PI * xi.x;
    float cos_theta = sqrt((1.0 - xi.y) / (1.0 + (a * a - 1.0) * xi.y));
    float sin_theta = sqrt(1.0 - cos_theta * cos_theta);

    float3 half_vector;
    half_vector.x = cos(phi) * sin_theta;
    half_vector.y = sin(phi) * sin_theta;
    half_vector.z = cos_theta;

    float3 up = abs(normal.z) < 0.999 ?
        float3(0.0, 0.0, 1.0) :
        float3(1.0, 0.0, 0.0);
    float3 tangent = normalize(cross(up, normal));
    float3 bitangent = cross(normal, tangent);
    float3 sample_vector =
        tangent * half_vector.x + bitangent * half_vector.y + normal * half_vector.z;
    return normalize(sample_vector);
}

float calculate_env_map_lod(float pdf, float width, float samples) {
    float omega_s = 1.0 / (samples * pdf + EPSILON);
    float omega_p = 4.0 * PI / (6.0 * width * width);
    return 0.5 * log2(omega_s / omega_p);
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

    float3 normal = get_sampling_vector(dispatch_thread_id);
    float3 reflection = normal;
    float3 view = reflection;

    if (Constants.roughness < 0.01) {
        float3 radiance =
            cubemap.SampleLevel(cubemap_sampler, reflection, 0.0).rgb;
        output_texture[dispatch_thread_id] = float4(radiance, 1.0);
        return;
    }

    float3 color = float3(0.0);
    float total_weight = 0.0;
    for (uint i = 0u; i < SAMPLE_COUNT; i++) {
        float2 xi = hammersley(i, SAMPLE_COUNT);
        float3 half_vector = importance_sample_ggx(xi, normal, Constants.roughness);
        float3 light = normalize(2.0 * dot(view, half_vector) * half_vector - view);
        float n_dot_l = max(dot(normal, light), 0.0);
        if (n_dot_l > 0.0) {
            float d = ndf_ggx(normal, half_vector, Constants.roughness);
            float n_dot_h = max(dot(normal, half_vector), 0.0);
            float h_dot_v = max(dot(half_vector, view), 0.0);
            float pdf = d * n_dot_h / (4.0 * h_dot_v) + EPSILON;
            float mip_level = calculate_env_map_lod(pdf, 1024.0, SAMPLE_COUNT);
            float3 sample_color =
                cubemap.SampleLevel(cubemap_sampler, light, mip_level).rgb;
            color += sample_color * n_dot_l;
            total_weight += n_dot_l;
        }
    }

    color /= total_weight;
    output_texture[dispatch_thread_id] = float4(color, 1.0);
}
