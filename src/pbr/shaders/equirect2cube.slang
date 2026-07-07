import lib.constants;
import lib.cubemap;

using namespace pbr.constants;
using namespace pbr.ibl;

layout(set = 0, binding = 0) Texture2D<float4> input_texture;
layout(set = 0, binding = 1) SamplerState input_sampler;
layout(set = 0, binding = 2, rgba32f) RWTexture2DArray<float4> output_texture;

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

    float3 vector = cube_face_sampling_vector(dispatch_thread_id, uint2(width, height));
    float phi = atan2(vector.z, vector.x);
    float theta = acos(vector.y);
    float4 color = input_texture.SampleLevel(
        input_sampler, float2(phi / TWO_PI, theta / PI), 0.0);
    output_texture[dispatch_thread_id] = color;
}
