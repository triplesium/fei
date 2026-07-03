#include "graphics/resource.hpp"
#include "rendering/shader.hpp"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <initializer_list>
#include <string>
#include <string_view>

using namespace fei;

namespace {

struct ExpectedBinding {
    std::string_view name;
    ResourceKind kind;
    uint32 set;
    uint32 binding;
    uint32 array_size {1};
};

void require_shader_resources(
    std::string_view shader_name,
    std::initializer_list<ExpectedBinding> expected
) {
    AssetPath shader_path("shader://" + std::string(shader_name));
    auto bindings_result = load_shader_reflection_bindings(shader_path);

    CAPTURE(shader_name);
    REQUIRE(bindings_result);
    const auto& bindings = *bindings_result;
    REQUIRE(bindings.size() == expected.size());

    for (const auto& resource : expected) {
        auto it = std::find_if(
            bindings.begin(),
            bindings.end(),
            [&](const ShaderResourceBinding& binding) {
                return binding.name == resource.name;
            }
        );

        CAPTURE(resource.name);
        REQUIRE(it != bindings.end());
        CHECK(it->kind == resource.kind);
        CHECK(it->set == resource.set);
        CHECK(it->binding == resource.binding);
        CHECK(it->array_size == resource.array_size);
    }
}

} // namespace

TEST_CASE(
    "PBR shaders have generated outputs for all backend paths",
    "[pbr][shader]"
) {
    AssetPath shader_path("shader://forward.frag");

    REQUIRE(compiled_opengl_shader_path(shader_path).has_value());
    REQUIRE(compiled_vulkan_shader_path(shader_path).has_value());
    REQUIRE(shader_reflection_path(shader_path).has_value());
}

TEST_CASE(
    "PBR forward shader resources match explicit set and binding layout",
    "[pbr][shader]"
) {
    require_shader_resources(
        "forward.frag",
        {
            {"View", ResourceKind::UniformBuffer, 0, 0},
            {"irradiance_map", ResourceKind::TextureReadOnly, 0, 1},
            {"radiance_map", ResourceKind::TextureReadOnly, 0, 2},
            {"brdf_lut", ResourceKind::TextureReadOnly, 0, 4},
            {"Mesh", ResourceKind::UniformBuffer, 1, 0},
            {"Material", ResourceKind::UniformBuffer, 2, 0},
            {"albedo_map", ResourceKind::TextureReadOnly, 2, 1},
            {"normal_map", ResourceKind::TextureReadOnly, 2, 2},
            {"metallic_map", ResourceKind::TextureReadOnly, 2, 3},
            {"roughness_map", ResourceKind::TextureReadOnly, 2, 4},
        }
    );
}

TEST_CASE(
    "PBR deferred shader resources match explicit set and binding layout",
    "[pbr][shader]"
) {
    require_shader_resources(
        "deferred_gi_direct.frag",
        {
            {"View", ResourceKind::UniformBuffer, 0, 0},
            {"g_position_ao", ResourceKind::TextureReadOnly, 1, 0},
            {"g_normal_roughness", ResourceKind::TextureReadOnly, 1, 1},
            {"g_albedo_metallic", ResourceKind::TextureReadOnly, 1, 2},
            {"g_specular", ResourceKind::TextureReadOnly, 1, 3},
            {"g_emissive_depth", ResourceKind::TextureReadOnly, 1, 4},
            {"Lighting", ResourceKind::UniformBuffer, 2, 0},
            {"shadow_map", ResourceKind::TextureReadOnly, 2, 1},
        }
    );

    require_shader_resources(
        "deferred_gi_indirect.frag",
        {
            {"View", ResourceKind::UniformBuffer, 0, 0},
            {"g_position_ao", ResourceKind::TextureReadOnly, 1, 0},
            {"g_normal_roughness", ResourceKind::TextureReadOnly, 1, 1},
            {"g_albedo_metallic", ResourceKind::TextureReadOnly, 1, 2},
            {"g_specular", ResourceKind::TextureReadOnly, 1, 3},
            {"g_emissive_depth", ResourceKind::TextureReadOnly, 1, 4},
            {"Vxgi", ResourceKind::UniformBuffer, 2, 0},
            {"voxel_visibility", ResourceKind::TextureReadOnly, 2, 1},
            {"voxel_tex", ResourceKind::TextureReadOnly, 2, 2},
            {"voxel_tex_mipmap", ResourceKind::TextureReadOnly, 2, 3, 6},
        }
    );

    require_shader_resources(
        "deferred_gi_composite.frag",
        {
            {"View", ResourceKind::UniformBuffer, 0, 0},
            {"g_position_ao", ResourceKind::TextureReadOnly, 1, 0},
            {"g_normal_roughness", ResourceKind::TextureReadOnly, 1, 1},
            {"g_albedo_metallic", ResourceKind::TextureReadOnly, 1, 2},
            {"g_specular", ResourceKind::TextureReadOnly, 1, 3},
            {"g_emissive_depth", ResourceKind::TextureReadOnly, 1, 4},
            {"direct_lighting", ResourceKind::TextureReadOnly, 2, 0},
            {"indirect_lighting", ResourceKind::TextureReadOnly, 2, 1},
        }
    );
}

TEST_CASE(
    "PBR VXGI shader resources match explicit set and binding layout",
    "[pbr][shader]"
) {
    require_shader_resources(
        "voxelization.frag",
        {
            {"Material", ResourceKind::UniformBuffer, 2, 0},
            {"albedo_map", ResourceKind::TextureReadOnly, 2, 1},
            {"emissive_map", ResourceKind::TextureReadOnly, 2, 5},
            {"voxel_albedo", ResourceKind::TextureReadWrite, 3, 0},
            {"voxel_normal", ResourceKind::TextureReadWrite, 3, 1},
            {"voxel_emissive", ResourceKind::TextureReadWrite, 3, 2},
            {"static_voxel_flag", ResourceKind::TextureReadWrite, 3, 4},
            {"VxgiVoxelization", ResourceKind::UniformBuffer, 4, 0},
        }
    );

    require_shader_resources(
        "aniso_mipmapbase.comp",
        {
            {"VxgiGenerateMipmapBase", ResourceKind::UniformBuffer, 0, 0},
            {"voxel_base", ResourceKind::TextureReadOnly, 0, 1},
            {"voxel_mipmap", ResourceKind::TextureReadWrite, 0, 2, 6},
        }
    );

    require_shader_resources(
        "aniso_mipmapvolume.comp",
        {
            {"VxgiGenerateMipmapVolume", ResourceKind::UniformBuffer, 0, 0},
            {"voxel_mipmap_dst", ResourceKind::TextureReadWrite, 0, 1, 6},
            {"voxel_mipmap_src", ResourceKind::TextureReadOnly, 0, 7, 6},
        }
    );

    require_shader_resources(
        "inject_radiance.comp",
        {
            {"voxel_albedo", ResourceKind::TextureReadWrite, 0, 0},
            {"voxel_normal", ResourceKind::TextureReadWrite, 0, 1},
            {"voxel_emissive", ResourceKind::TextureReadWrite, 0, 2},
            {"voxel_radiance", ResourceKind::TextureReadWrite, 0, 3},
            {"VxgiVoxelization", ResourceKind::UniformBuffer, 1, 0},
            {"Lighting", ResourceKind::UniformBuffer, 2, 0},
            {"shadow_map", ResourceKind::TextureReadOnly, 2, 1},
            {"VxgiInjectRadiance", ResourceKind::UniformBuffer, 3, 0},
        }
    );

    require_shader_resources(
        "inject_propagation.comp",
        {
            {"VxgiInjectPropagation", ResourceKind::UniformBuffer, 0, 0},
            {"voxel_composite", ResourceKind::TextureReadWrite, 0, 1},
            {"voxel_albedo", ResourceKind::TextureReadOnly, 0, 2},
            {"voxel_normal", ResourceKind::TextureReadOnly, 0, 3},
            {"voxel_tex_mipmap", ResourceKind::TextureReadOnly, 0, 4, 6},
        }
    );
}
