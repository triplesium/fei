#include "common.slangh"

struct FragmentInput {
    [vk::location(0)] float2 tex_coords : TEXCOORD0;
};

layout(set = 0, binding = 0) ConstantBuffer<ViewData> View;
layout(set = 1, binding = 0) Texture2D<float4> g_position_ao;
layout(set = 1, binding = 1) Texture2D<float4> g_normal_roughness;
layout(set = 1, binding = 2) Texture2D<float4> g_albedo_metallic;
layout(set = 1, binding = 3) Texture2D<float4> g_specular;
layout(set = 1, binding = 5) SamplerState g_buffer_sampler;
layout(set = 2, binding = 0) ConstantBuffer<VxgiData> Vxgi;
layout(set = 2, binding = 2) Texture3D<float4> voxel_tex;
layout(set = 2, binding = 3) Texture3D<float4> voxel_tex_mipmap[6];
layout(set = 2, binding = 9) SamplerState voxel_sampler;

static const float PI = 3.14159265;
static const float HALF_PI = 1.57079;

static const float3 diffuse_cone_directions[6] = {
    float3(0.0, 1.0, 0.0),
    float3(0.0, 0.5, 0.866025),
    float3(0.823639, 0.5, 0.267617),
    float3(0.509037, 0.5, -0.7006629),
    float3(-0.50937, 0.5, -0.7006629),
    float3(-0.823639, 0.5, 0.267617),
};

static const float diffuse_cone_weights[6] = {
    PI / 4.0,
    3.0 * PI / 20.0,
    3.0 * PI / 20.0,
    3.0 * PI / 20.0,
    3.0 * PI / 20.0,
    3.0 * PI / 20.0,
};

float3 srgb_to_linear(float3 value) {
    return pow(max(value, float3(0.0)), float3(2.2));
}

float3 world_to_voxel(float3 position) {
    return (position - Vxgi.world_min_point) * Vxgi.voxel_scale;
}

float4 anisotropic_sample(float3 coord, float3 weight, uint3 face, float lod) {
    float aniso_level = max(lod - 1.0, 0.0);
    float4 aniso_sample =
        weight.x * voxel_tex_mipmap[face.x].SampleLevel(voxel_sampler, coord, aniso_level) +
        weight.y * voxel_tex_mipmap[face.y].SampleLevel(voxel_sampler, coord, aniso_level) +
        weight.z * voxel_tex_mipmap[face.z].SampleLevel(voxel_sampler, coord, aniso_level);

    if (lod < 1.0) {
        float4 base_color = voxel_tex.Sample(voxel_sampler, coord);
        aniso_sample = lerp(base_color, aniso_sample, clamp(lod, 0.0, 1.0));
    }

    return aniso_sample;
}

bool intersect_ray_with_world_aabb(float3 ray_origin, float3 ray_direction, out float enter, out float leave) {
    float3 temp_min = (Vxgi.world_min_point - ray_origin) / ray_direction;
    float3 temp_max = (Vxgi.world_max_point - ray_origin) / ray_direction;
    float3 v3_max = max(temp_max, temp_min);
    float3 v3_min = min(temp_max, temp_min);
    leave = min(v3_max.x, min(v3_max.y, v3_max.z));
    enter = max(max(v3_min.x, 0.0), max(v3_min.y, v3_min.z));
    return leave > enter;
}

float4 trace_cone(
    float3 position,
    float3 normal,
    float3 direction,
    float aperture,
    bool trace_occlusion
) {
    uint3 visible_face;
    visible_face.x = direction.x < 0.0 ? 0 : 1;
    visible_face.y = direction.y < 0.0 ? 2 : 3;
    visible_face.z = direction.z < 0.0 ? 4 : 5;
    trace_occlusion = trace_occlusion && Vxgi.ao_alpha < 1.0;

    float voxel_world_size = 2.0 / (Vxgi.voxel_scale * Vxgi.volume_dimension);
    float3 weight = direction * direction;
    float dst = voxel_world_size;
    float3 start_position = position + normal * dst;
    float4 cone_sample = float4(0.0);
    float occlusion = 0.0;
    float max_distance = Vxgi.max_tracing_distance_global * (1.0 / Vxgi.voxel_scale);
    float falloff = 0.5 * Vxgi.ao_falloff * Vxgi.voxel_scale;
    float enter = 0.0;
    float leave = 0.0;

    if (!intersect_ray_with_world_aabb(position, direction, enter, leave)) {
        cone_sample.a = 1.0;
    }

    while (cone_sample.a < 1.0 && dst <= max_distance) {
        float3 cone_position = start_position + direction * dst;
        float diameter = 2.0 * aperture * dst;
        float mip_level = log2(diameter / voxel_world_size);
        float3 coord = world_to_voxel(cone_position);
        float4 aniso_sample = anisotropic_sample(coord, weight, visible_face, mip_level);
        cone_sample += (1.0 - cone_sample.a) * aniso_sample;

        if (trace_occlusion && occlusion < 1.0) {
            occlusion += ((1.0 - occlusion) * aniso_sample.a) /
                (1.0 + falloff * diameter);
        }

        dst += diameter * Vxgi.sampling_factor;
    }

    return float4(cone_sample.rgb, occlusion);
}

float4 calculate_indirect_lighting(
    float3 position,
    float3 normal,
    float3 albedo,
    float4 specular,
    bool ambient_occlusion
) {
    float4 specular_trace = float4(0.0);
    float4 diffuse_trace = float4(0.0);
    float3 cone_direction = float3(0.0);

    if (specular.r > 0.0 || specular.g > 0.0 || specular.b > 0.0) {
        float3 view_direction = normalize(View.world_position - position);
        cone_direction = normalize(reflect(-view_direction, normal));
        float aperture = clamp(tan(HALF_PI * (1.0 - specular.a)), 0.0174533, PI);
        specular_trace = trace_cone(position, normal, cone_direction, aperture, false);
        specular_trace.rgb *= specular.rgb;
    }

    if (albedo.r > 0.0 || albedo.g > 0.0 || albedo.b > 0.0) {
        const float aperture = 0.57735;
        float3 guide = float3(0.0, 1.0, 0.0);
        if (abs(dot(normal, guide)) == 1.0) {
            guide = float3(0.0, 0.0, 1.0);
        }

        float3 right = normalize(guide - dot(normal, guide) * normal);
        float3 up = cross(right, normal);

        for (int i = 0; i < 6; i++) {
            cone_direction = normal;
            cone_direction += diffuse_cone_directions[i].x * right +
                diffuse_cone_directions[i].z * up;
            cone_direction = normalize(cone_direction);
            diffuse_trace += trace_cone(
                position,
                normal,
                cone_direction,
                aperture,
                ambient_occlusion
            ) * diffuse_cone_weights[i];
        }

        diffuse_trace.rgb *= albedo;
    }

    float3 result = Vxgi.bounce_strength * (diffuse_trace.rgb + specular_trace.rgb);
    float alpha = ambient_occlusion ?
        clamp(1.0 - diffuse_trace.a + Vxgi.ao_alpha, 0.0, 1.0) :
        1.0;
    return float4(result, alpha);
}

[shader("fragment")]
float4 fragment_main(FragmentInput input) : SV_Target0 {
    float3 position = g_position_ao.Sample(g_buffer_sampler, input.tex_coords).rgb;
    float3 normal =
        normalize(g_normal_roughness.Sample(g_buffer_sampler, input.tex_coords).xyz);
    float4 specular = g_specular.Sample(g_buffer_sampler, input.tex_coords);
    float3 albedo =
        srgb_to_linear(g_albedo_metallic.Sample(g_buffer_sampler, input.tex_coords).rgb);
    return calculate_indirect_lighting(position, normal, albedo, specular, true);
}
