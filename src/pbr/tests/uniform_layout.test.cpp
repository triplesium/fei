#include "pbr/environment_map.hpp"
#include "pbr/light.hpp"
#include "pbr/material.hpp"
#include "pbr/vxgi.hpp"
#include "rendering/mesh/mesh_uniform.hpp"
#include "rendering/view.hpp"

#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <type_traits>

using namespace fei;

namespace {

template<typename T>
void require_standard_uniform_layout(std::size_t size) {
    static_assert(std::is_standard_layout_v<T>);
    CHECK(alignof(T) == 16);
    CHECK(sizeof(T) == size);
}

} // namespace

TEST_CASE("PBR uniform scalar types keep shader ABI sizes", "[pbr][uniform]") {
    static_assert(std::is_standard_layout_v<Vector3>);
    static_assert(std::is_standard_layout_v<Color3F>);
    static_assert(std::is_standard_layout_v<Matrix4x4>);

    CHECK(alignof(Vector3) == 4);
    CHECK(sizeof(Vector3) == 12);
    CHECK(alignof(Color3F) == 4);
    CHECK(sizeof(Color3F) == 12);
    CHECK(alignof(Matrix4x4) == 4);
    CHECK(sizeof(Matrix4x4) == 64);
}

TEST_CASE(
    "PBR view and mesh uniforms keep shader ABI layout",
    "[pbr][uniform]"
) {
    require_standard_uniform_layout<ViewUniform>(336);
    CHECK(offsetof(ViewUniform, clip_from_world) == 0);
    CHECK(offsetof(ViewUniform, view_from_world) == 64);
    CHECK(offsetof(ViewUniform, clip_from_view) == 128);
    CHECK(offsetof(ViewUniform, world_from_view) == 192);
    CHECK(offsetof(ViewUniform, view_from_clip) == 256);
    CHECK(offsetof(ViewUniform, world_position) == 320);

    require_standard_uniform_layout<MeshUniform>(64);
    CHECK(offsetof(MeshUniform, world_from_local) == 0);
}

TEST_CASE("PBR material uniform keeps shader ABI layout", "[pbr][uniform]") {
    require_standard_uniform_layout<StandardMaterialUniform>(64);
    CHECK(offsetof(StandardMaterialUniform, albedo) == 0);
    CHECK(offsetof(StandardMaterialUniform, metallic) == 12);
    CHECK(offsetof(StandardMaterialUniform, roughness) == 16);
    CHECK(offsetof(StandardMaterialUniform, emissive) == 32);
    CHECK(offsetof(StandardMaterialUniform, specular) == 48);
    CHECK(offsetof(StandardMaterialUniform, flags) == 60);
}

TEST_CASE(
    "PBR environment map uniform keeps shader ABI layout",
    "[pbr][uniform]"
) {
    require_standard_uniform_layout<EnvironmentMapUniform>(80);
    CHECK(offsetof(EnvironmentMapUniform, environment_from_world) == 0);
    CHECK(offsetof(EnvironmentMapUniform, intensity) == 64);
    CHECK(offsetof(EnvironmentMapUniform, max_specular_lod) == 68);
    CHECK(offsetof(EnvironmentMapUniform, enabled) == 72);
    CHECK(offsetof(EnvironmentMapUniform, padding) == 76);
}

TEST_CASE(
    "PBR environment map light can disable IBL",
    "[pbr][environment_map]"
) {
    EnvironmentMapLight light {.intensity = 2.0f};
    CHECK(light.enabled);

    light.enabled = false;
    CHECK_FALSE(light.enabled);
    CHECK(light.intensity == 2.0f);
}

TEST_CASE("PBR lighting uniform keeps shader ABI layout", "[pbr][uniform]") {
    using Light = LightingUniform::Light;
    using Attenuation = Light::Attenuation;

    require_standard_uniform_layout<Attenuation>(16);
    CHECK(offsetof(Attenuation, constant) == 0);
    CHECK(offsetof(Attenuation, linear) == 4);
    CHECK(offsetof(Attenuation, quadratic) == 8);

    require_standard_uniform_layout<Light>(112);
    CHECK(offsetof(Light, angle_inner_cone) == 0);
    CHECK(offsetof(Light, angle_outer_cone) == 4);
    CHECK(offsetof(Light, ambient) == 16);
    CHECK(offsetof(Light, diffuse) == 32);
    CHECK(offsetof(Light, specular) == 48);
    CHECK(offsetof(Light, position) == 64);
    CHECK(offsetof(Light, direction) == 80);
    CHECK(offsetof(Light, shadowing_method) == 92);
    CHECK(offsetof(Light, attenuation) == 96);

    require_standard_uniform_layout<LightingUniform>(1760);
    CHECK(offsetof(LightingUniform, directional_lights) == 0);
    CHECK(offsetof(LightingUniform, point_lights) == 336);
    CHECK(offsetof(LightingUniform, spot_lights) == 1008);
    CHECK(offsetof(LightingUniform, num_directional_lights) == 1680);
    CHECK(offsetof(LightingUniform, num_point_lights) == 1684);
    CHECK(offsetof(LightingUniform, num_spot_lights) == 1688);
    CHECK(offsetof(LightingUniform, light_view_projection) == 1696);
}

TEST_CASE("PBR VXGI uniforms keep shader ABI layout", "[pbr][uniform]") {
    require_standard_uniform_layout<VxgiVoxelizationUniform>(416);
    CHECK(offsetof(VxgiVoxelizationUniform, view_projections) == 0);
    CHECK(offsetof(VxgiVoxelizationUniform, inv_view_projections) == 192);
    CHECK(offsetof(VxgiVoxelizationUniform, volume_dimension) == 384);
    CHECK(offsetof(VxgiVoxelizationUniform, flag_static_voxels) == 388);
    CHECK(offsetof(VxgiVoxelizationUniform, voxel_scale) == 392);
    CHECK(offsetof(VxgiVoxelizationUniform, voxel_size) == 396);
    CHECK(offsetof(VxgiVoxelizationUniform, world_min_point) == 400);

    require_standard_uniform_layout<VxgiGenerateMipmapBase::Uniform>(16);
    CHECK(offsetof(VxgiGenerateMipmapBase::Uniform, mip_dimension) == 0);

    require_standard_uniform_layout<VxgiGenerateMipmapVolume::Uniform>(16);
    CHECK(offsetof(VxgiGenerateMipmapVolume::Uniform, mip_dimension) == 0);
    CHECK(offsetof(VxgiGenerateMipmapVolume::Uniform, mip_level) == 12);

    require_standard_uniform_layout<VxgiInjectRadianceUniform>(16);
    CHECK(offsetof(VxgiInjectRadianceUniform, normal_weighted_lambert) == 0);
    CHECK(offsetof(VxgiInjectRadianceUniform, trace_shadow_hit) == 4);

    require_standard_uniform_layout<VxgiInjectPropagationUniform>(16);
    CHECK(
        offsetof(VxgiInjectPropagationUniform, max_tracing_distance_global) == 0
    );
    CHECK(offsetof(VxgiInjectPropagationUniform, volume_dimension) == 4);
    CHECK(offsetof(VxgiInjectPropagationUniform, check_boundaries) == 8);

    require_standard_uniform_layout<VxgiUniform>(80);
    CHECK(offsetof(VxgiUniform, voxel_scale) == 0);
    CHECK(offsetof(VxgiUniform, world_min_point) == 16);
    CHECK(offsetof(VxgiUniform, world_max_point) == 32);
    CHECK(offsetof(VxgiUniform, volume_dimension) == 44);
    CHECK(offsetof(VxgiUniform, max_tracing_distance_global) == 48);
    CHECK(offsetof(VxgiUniform, bounce_strength) == 52);
    CHECK(offsetof(VxgiUniform, ao_falloff) == 56);
    CHECK(offsetof(VxgiUniform, ao_alpha) == 60);
    CHECK(offsetof(VxgiUniform, sampling_factor) == 64);
    CHECK(offsetof(VxgiUniform, mode) == 68);
    CHECK(offsetof(VxgiUniform, skylight_leaking) == 72);
}
