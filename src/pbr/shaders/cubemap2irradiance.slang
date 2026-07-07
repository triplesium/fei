import lib.constants;
import lib.cubemap;

using namespace pbr.constants;
using namespace pbr.ibl;

layout(set = 0, binding = 0) TextureCube<float4> cubemap;
layout(set = 0, binding = 1) SamplerState cubemap_sampler;
layout(set = 0, binding = 2, rgba32f) RWTexture2DArray<float4> output_texture;

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
