layout(set = 0, binding = 0) TextureCube<float4> cubemap;
layout(set = 0, binding = 1) SamplerState cubemap_sampler;
layout(set = 0, binding = 2, rgba32f) RWTexture2DArray<float4> output_texture;

static const float PI = 3.14159265359;
static const float TWO_PI = 2.0 * PI;

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
    float3 irradiance = float3(0.0);
    float3 up = abs(normal.y) > 0.999 ?
        float3(1.0, 0.0, 0.0) :
        float3(0.0, 1.0, 0.0);
    float3 right = normalize(cross(up, normal));
    up = normalize(cross(normal, right));

    float sample_delta = 0.02;
    float sample_count = 0.0;
    for (float phi = 0.0; phi < TWO_PI; phi += sample_delta) {
        for (float theta = 0.0; theta < 0.5 * PI; theta += sample_delta) {
            float3 tangent_sample =
                float3(sin(theta) * cos(phi), sin(theta) * sin(phi), cos(theta));
            float3 sample_vector =
                tangent_sample.x * right +
                tangent_sample.y * up +
                tangent_sample.z * normal;
            float3 sampled =
                cubemap.SampleLevel(cubemap_sampler, sample_vector, 0.0).rgb;
            float luminance = dot(sampled, float3(0.2126, 0.7152, 0.0722));
            if (luminance > 50.0) {
                sampled *= 50.0 / luminance;
            }
            irradiance += sampled * cos(theta) * sin(theta);
            sample_count++;
        }
    }

    irradiance = PI * irradiance * (1.0 / sample_count);
    output_texture[dispatch_thread_id] = float4(irradiance, 1.0);
}
