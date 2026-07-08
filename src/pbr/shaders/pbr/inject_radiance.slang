import rendering.constants;
import rendering.normal;
import pbr.lighting.types;
import pbr.lighting.evsm;
import pbr.vxgi.types;

using namespace rendering.constants;
using namespace pbr.lighting.evsm;
using namespace rendering.normal;

layout(set = 0, binding = 0, rgba8) RWTexture3D<float4> voxel_albedo;
layout(set = 0, binding = 1, rgba8) RWTexture3D<float4> voxel_normal;
layout(set = 0, binding = 2, rgba8) RWTexture3D<float4> voxel_emissive;
layout(set = 0, binding = 3, rgba8) RWTexture3D<float4> voxel_radiance;
layout(set = 1, binding = 0) ConstantBuffer<VxgiVoxelizationUniform> VxgiVoxelization;
layout(set = 2, binding = 0) ConstantBuffer<LightingUniform> Lighting;
layout(set = 2, binding = 1) Texture2D<float4> shadow_map;
layout(set = 2, binding = 2) SamplerState shadow_map_sampler;

struct VxgiInjectRadianceUniform {
    uint normal_weighted_lambert;
    float trace_shadow_hit;
};

layout(set = 3, binding = 0) ConstantBuffer<VxgiInjectRadianceUniform> VxgiInjectRadiance;

float trace_shadow(float3 position, float3 direction, float max_tracing_distance) {
    float k = VxgiInjectRadiance.trace_shadow_hit *
        VxgiInjectRadiance.trace_shadow_hit;
    float voxel_tex_size = VxgiVoxelization.voxel_texel_size;
    float dst = voxel_tex_size * 2.0;
    float3 sample_position = direction * dst + position;
    float visibility = 0.0;

    while (visibility <= 1.0 && dst <= max_tracing_distance) {
        if (sample_position.x < 0.0 ||
            sample_position.y < 0.0 ||
            sample_position.z < 0.0 ||
            sample_position.x > 1.0 ||
            sample_position.y > 1.0 ||
            sample_position.z > 1.0) {
            break;
        }

        float trace_sample =
            ceil(voxel_albedo[VxgiVoxelization.voxel_index(sample_position)].a) *
            k;
        if (trace_sample > 1.0 - EPSILON) {
            return 0.0;
        }

        visibility += (1.0 - visibility) * trace_sample / dst;
        dst += voxel_tex_size;
        sample_position = direction * dst + position;
    }

    return 1.0 - visibility;
}

float3 brdf(Light light, float3 normal, float3 albedo) {
    float normal_dot_light = 0.0;
    if (VxgiInjectRadiance.normal_weighted_lambert == 1) {
        float3 weight = normal * normal;
        float r_dot_l = dot(float3(1.0, 0.0, 0.0), light.direction);
        float u_dot_l = dot(float3(0.0, 1.0, 0.0), light.direction);
        float f_dot_l = dot(float3(0.0, 0.0, 1.0), light.direction);

        r_dot_l = normal.x > 0.0 ? max(r_dot_l, 0.0) : max(-r_dot_l, 0.0);
        u_dot_l = normal.y > 0.0 ? max(u_dot_l, 0.0) : max(-u_dot_l, 0.0);
        f_dot_l = normal.z > 0.0 ? max(f_dot_l, 0.0) : max(-f_dot_l, 0.0);
        normal_dot_light = r_dot_l * weight.x + u_dot_l * weight.y + f_dot_l * weight.z;
    } else {
        normal_dot_light = max(dot(normal, light.direction), 0.0);
    }

    return light.diffuse * albedo * normal_dot_light;
}

float4 calculate_directional(
    Light light,
    float3 normal,
    float3 position,
    float3 albedo
) {
    float light_visibility = 1.0;
    if (light.shadowing_method == 1) {
        light_visibility = visibility_level(
            shadow_map,
            shadow_map_sampler,
            Lighting.light_view_projection,
            position
        );
    } else if (light.shadowing_method == 2) {
        float3 voxel_position = VxgiVoxelization.world_to_voxel(position);
        light_visibility = trace_shadow(voxel_position, light.direction, 1.0);
    }

    if (light_visibility == 0.0) {
        return float4(0.0);
    }

    return float4(brdf(light, normal, albedo) * light_visibility, light_visibility);
}

float4 calculate_point(Light light, float3 normal, float3 position, float3 albedo) {
    light.direction = light.position - position;
    float d = length(light.direction);
    light.direction = normalize(light.direction);
    float falloff = 1.0 /
        (light.attenuation.constant + light.attenuation.linear * d +
         light.attenuation.quadratic * d * d + 1.0);
    if (falloff <= 0.0) {
        return float4(0.0);
    }

    float light_visibility = 1.0;
    if (light.shadowing_method == 2) {
        float3 voxel_position = VxgiVoxelization.world_to_voxel(position);
        float3 light_position = VxgiVoxelization.world_to_voxel(light.position);
        float3 light_dir = light_position - voxel_position;
        float trace_distance = length(light_dir);
        light_dir = normalize(light_dir);
        light_visibility = trace_shadow(voxel_position, light_dir, trace_distance);
    }

    if (light_visibility <= 0.0) {
        return float4(0.0);
    }

    return float4(brdf(light, normal, albedo) * falloff * light_visibility, light_visibility);
}

float4 calculate_spot(Light light, float3 normal, float3 position, float3 albedo) {
    float3 spot_direction = light.direction;
    light.direction = normalize(light.position - position);
    float cos_angle = dot(-light.direction, spot_direction);
    if (cos_angle < light.angle_outer_cone) {
        return float4(0.0);
    }

    float inner_minus_outer = light.angle_inner_cone - light.angle_outer_cone;
    float spot_mark = (cos_angle - light.angle_outer_cone) / inner_minus_outer;
    float spot_falloff = smoothstep(0.0, 1.0, spot_mark);
    if (spot_falloff <= 0.0) {
        return float4(0.0);
    }

    float distance_to_light = distance(light.position, position);
    float falloff = 1.0 /
        (light.attenuation.constant + light.attenuation.linear * distance_to_light +
         light.attenuation.quadratic * distance_to_light * distance_to_light + 1.0);
    if (falloff <= 0.0) {
        return float4(0.0);
    }

    float light_visibility = 1.0;
    if (light.shadowing_method == 2) {
        float3 voxel_position = VxgiVoxelization.world_to_voxel(position);
        float3 light_position = VxgiVoxelization.world_to_voxel(light.position);
        float3 light_dir = light_position - voxel_position;
        float trace_distance = length(light_dir);
        light_dir = normalize(light_dir);
        light_visibility = trace_shadow(voxel_position, light_dir, trace_distance);
    }

    if (light_visibility <= 0.0) {
        return float4(0.0);
    }

    return float4(
        brdf(light, normal, albedo) * falloff * spot_falloff * light_visibility,
        light_visibility
    );
}

float4 calculate_direct_lighting(float3 position, float3 normal, float3 albedo) {
    normal = normalize(normal);
    float voxel_world_size = VxgiVoxelization.voxel_world_size;
    float4 direct_lighting = float4(0.0);
    float4 current = float4(0.0);
    uint count = 0;

    position = position + normal * voxel_world_size * 0.5;

    for (uint i = 0; i < Lighting.num_directional_lights; ++i) {
        current = calculate_directional(
            Lighting.directional_lights[i],
            normal,
            position,
            albedo
        );
        direct_lighting.rgb += current.rgb;
        direct_lighting.a += current.a;
        count++;
    }

    for (uint i = 0; i < Lighting.num_point_lights; ++i) {
        current = calculate_point(Lighting.point_lights[i], normal, position, albedo);
        direct_lighting.rgb += current.rgb;
        direct_lighting.a += current.a;
        count++;
    }

    for (uint i = 0; i < Lighting.num_spot_lights; ++i) {
        current = calculate_spot(Lighting.spot_lights[i], normal, position, albedo);
        direct_lighting.rgb += current.rgb;
        direct_lighting.a += current.a;
        count++;
    }

    if (count > 0) {
        direct_lighting.a /= count;
    }

    return direct_lighting;
}

[shader("compute")]
[numthreads(8, 8, 8)]
void compute_main(uint3 dispatch_thread_id : SV_DispatchThreadID) {
    if (!VxgiVoxelization.contains(dispatch_thread_id)) {
        return;
    }

    int3 write_pos = int3(dispatch_thread_id);
    float4 albedo = voxel_albedo[write_pos];
    if (albedo.a < EPSILON) {
        return;
    }

    albedo.a = 0.0;
    float3 base_normal = voxel_normal[write_pos].xyz;
    float3 normal = decode(base_normal);
    float3 emissive = voxel_emissive[write_pos].rgb;

    if (albedo.r > 0.0 || albedo.g > 0.0 || albedo.b > 0.0) {
        float3 world_position = VxgiVoxelization.voxel_to_world(write_pos);
        albedo = calculate_direct_lighting(world_position, normal, albedo.rgb);
    }

    voxel_normal[write_pos] = float4(base_normal, albedo.a);
    albedo.rgb += emissive;
    albedo.a = 1.0;
    voxel_radiance[write_pos] = albedo;
}
