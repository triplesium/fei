layout(set = 0, binding = 0) Texture2D<float4> input_texture;
layout(set = 0, binding = 1) SamplerState input_sampler;
layout(set = 0, binding = 2, rgba32f) RWTexture2DArray<float4> output_texture;

static const float PI = 3.141592;
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
[numthreads(32, 32, 1)]
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

    float3 vector = get_sampling_vector(dispatch_thread_id);
    float phi = atan2(vector.z, vector.x);
    float theta = acos(vector.y);
    float4 color =
        input_texture.Sample(input_sampler, float2(phi / TWO_PI, theta / PI));
    output_texture[dispatch_thread_id] = color;
}
