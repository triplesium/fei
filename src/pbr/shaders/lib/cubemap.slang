import lib.constants;

namespace pbr.ibl {

using namespace pbr.constants;

public float3 cube_face_sampling_vector(uint3 dispatch_thread_id, uint2 size) {
    float2 st = float2(dispatch_thread_id.xy) / float2(size);
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

public float radical_inverse_vdc(uint bits) {
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10;
}

public float2 hammersley(uint index, uint count) {
    return float2(float(index) / float(count), radical_inverse_vdc(index));
}

public float3 importance_sample_ggx(float2 xi, float3 normal, float roughness) {
    float a = roughness * roughness;
    float phi = TWO_PI * xi.x;
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

public float calculate_env_map_lod(float pdf, float width, float samples) {
    float omega_s = 1.0 / (samples * pdf + 0.00001);
    float omega_p = 4.0 * PI / (6.0 * width * width);
    return 0.5 * log2(omega_s / omega_p);
}

} // namespace pbr.ibl
