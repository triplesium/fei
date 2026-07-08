import pbr.deferred.gbuffer;
import rendering.constants;
import rendering.fullscreen;
import rendering.view;
import pbr.vxgi.basis;
import pbr.vxgi.types;

using namespace rendering.constants;
using namespace pbr.deferred;
using namespace pbr.vxgi;

layout(set = 0, binding = 0) ConstantBuffer<ViewUniform> View;
layout(set = 1, binding = 0) Texture2D<float4> g_position_ao;
layout(set = 1, binding = 1) Texture2D<float4> g_normal_roughness;
layout(set = 1, binding = 2) Texture2D<float4> g_albedo_metallic;
layout(set = 1, binding = 3) Texture2D<float4> g_specular;
layout(set = 1, binding = 5) SamplerState g_buffer_sampler;
layout(set = 2, binding = 0) ConstantBuffer<VxgiUniform> Vxgi;
layout(set = 2, binding = 2) Texture3D<float4> voxel_tex;
layout(set = 2, binding = 3) Texture3D<float4> voxel_tex_mipmap[6];
layout(set = 2, binding = 9) SamplerState voxel_sampler;

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

float4 trace_cone(
    float3 position,
    float3 normal,
    float3 direction,
    float aperture,
    bool trace_occlusion
) {
    uint3 visible_face = visible_aniso_faces(direction);
    trace_occlusion = trace_occlusion && Vxgi.ao_alpha < 1.0;

    float voxel_world_size = Vxgi.cone_trace_voxel_world_size;
    float3 weight = direction * direction;
    float dst = voxel_world_size;
    float3 start_position = position + normal * dst;
    float4 cone_sample = float4(0.0);
    float occlusion = 0.0;
    float max_distance = Vxgi.max_tracing_distance_global * Vxgi.inverse_voxel_scale;
    float falloff = 0.5 * Vxgi.ao_falloff * Vxgi.voxel_scale;
    float enter = 0.0;
    float leave = 0.0;

    if (!Vxgi.intersect_world_aabb(position, direction, enter, leave)) {
        cone_sample.a = 1.0;
    }

    while (cone_sample.a < 1.0 && dst <= max_distance) {
        float3 cone_position = start_position + direction * dst;
        float diameter = 2.0 * aperture * dst;
        float mip_level = log2(diameter / voxel_world_size);
        float3 coord = Vxgi.world_to_voxel(cone_position);
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
    GBufferSurface surface,
    bool ambient_occlusion
) {
    float4 specular_trace = float4(0.0);
    float4 diffuse_trace = float4(0.0);
    float3 cone_direction = float3(0.0);

    if (surface.has_specular()) {
        float3 view_direction = View.view_direction(surface.position);
        cone_direction = normalize(reflect(-view_direction, surface.normal));
        float aperture =
            clamp(tan(HALF_PI * (1.0 - surface.specular_alpha)), 0.0174533, PI);
        specular_trace =
            trace_cone(
                surface.position,
                surface.normal,
                cone_direction,
                aperture,
                false
            );
        specular_trace.rgb *= surface.specular;
    }

    if (surface.has_albedo()) {
        const float aperture = 0.57735;
        OrthonormalBasis basis = OrthonormalBasis.from_normal(surface.normal);

        for (int i = 0; i < 6; i++) {
            cone_direction =
                basis.cone_direction(surface.normal, diffuse_cone_directions[i]);
            diffuse_trace += trace_cone(
                surface.position,
                surface.normal,
                cone_direction,
                aperture,
                ambient_occlusion
            ) * diffuse_cone_weights[i];
        }

        diffuse_trace.rgb *= surface.albedo;
    }

    float3 result = Vxgi.bounce_strength * (diffuse_trace.rgb + specular_trace.rgb);
    float alpha = ambient_occlusion ?
        clamp(1.0 - diffuse_trace.a + Vxgi.ao_alpha, 0.0, 1.0) :
        1.0;
    return float4(result, alpha);
}

[shader("fragment")]
float4 fragment_main(FullscreenFragmentInput input) : SV_Target0 {
    GBufferSurface surface = read_lighting_surface(
        g_position_ao,
        g_normal_roughness,
        g_albedo_metallic,
        g_specular,
        g_buffer_sampler,
        input.tex_coords
    );
    return calculate_indirect_lighting(surface, true);
}
