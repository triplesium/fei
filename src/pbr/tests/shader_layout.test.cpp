#include "graphics/resource.hpp"
#include "rendering/shader.hpp"

#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstring>
#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

using namespace fei;

namespace {

struct ExpectedBinding {
    std::string_view name;
    ResourceKind kind;
    uint32 set;
    uint32 binding;
    uint32 array_size {1};
};

constexpr uint32 SpirvOpExecutionMode = 16;
constexpr uint32 SpirvExecutionModePixelCenterInteger = 6;

constexpr std::array<std::string_view, 25> PbrShaderNames {
    "aniso_mipmapbase.comp",
    "aniso_mipmapvolume.comp",
    "blur.frag",
    "color.frag",
    "cubemap2irradiance.comp",
    "cubemap2radiance.comp",
    "deferred_gi_composite.frag",
    "deferred_gi_direct.frag",
    "deferred_gi_indirect.frag",
    "deferred_prepass.frag",
    "deferred_prepass.vert",
    "deferred_present.frag",
    "equirect2cube.comp",
    "forward.frag",
    "forward.vert",
    "inject_propagation.comp",
    "inject_radiance.comp",
    "quad.vert",
    "shadow.frag",
    "shadow.vert",
    "skybox.frag",
    "skybox.vert",
    "voxelization.frag",
    "voxelization.geom",
    "voxelization.vert",
};

std::vector<uint32> read_vulkan_shader_words(std::string_view shader_name) {
    AssetPath shader_path("shader://" + std::string(shader_name));
    auto path = compiled_vulkan_shader_path(shader_path);
    CAPTURE(shader_name);
    REQUIRE(path.has_value());

    auto binary = read_shader_binary(*path);
    REQUIRE(binary);
    REQUIRE(binary->size() % sizeof(uint32) == 0);

    std::vector<uint32> words(binary->size() / sizeof(uint32));
    std::memcpy(words.data(), binary->data(), binary->size());
    REQUIRE(words.size() >= 5);
    REQUIRE(words.front() == 0x07230203);
    return words;
}

bool has_spirv_execution_mode(
    const std::vector<uint32>& words,
    uint32 execution_mode
) {
    constexpr std::size_t HeaderWordCount = 5;
    for (std::size_t index = HeaderWordCount; index < words.size();) {
        const uint32 instruction = words[index];
        const uint32 word_count = instruction >> 16;
        const uint32 opcode = instruction & 0xffff;
        if (word_count == 0 || index + word_count > words.size()) {
            FAIL("Malformed SPIR-V instruction stream");
        }

        if (opcode == SpirvOpExecutionMode && word_count >= 3 &&
            words[index + 2] == execution_mode) {
            return true;
        }

        index += word_count;
    }
    return false;
}

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
    for (auto shader_name : PbrShaderNames) {
        AssetPath shader_path("shader://" + std::string(shader_name));
        CAPTURE(shader_name);

        REQUIRE(compiled_opengl_shader_path(shader_path).has_value());
        REQUIRE(compiled_vulkan_shader_path(shader_path).has_value());
        REQUIRE(shader_reflection_path(shader_path).has_value());
    }
}

TEST_CASE(
    "PBR Vulkan shader outputs avoid unsupported pixel-center execution mode",
    "[pbr][shader]"
) {
    for (auto shader_name : PbrShaderNames) {
        CAPTURE(shader_name);
        CHECK_FALSE(has_spirv_execution_mode(
            read_vulkan_shader_words(shader_name),
            SpirvExecutionModePixelCenterInteger
        ));
    }
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
            {"cubemap_sampler", ResourceKind::Sampler, 0, 3},
            {"brdf_lut", ResourceKind::TextureReadOnly, 0, 4},
            {"Material", ResourceKind::UniformBuffer, 2, 0},
            {"sampler", ResourceKind::Sampler, 2, 7},
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
            {"g_buffer_sampler", ResourceKind::Sampler, 1, 5},
            {"Lighting", ResourceKind::UniformBuffer, 2, 0},
            {"shadow_map", ResourceKind::TextureReadOnly, 2, 1},
            {"shadow_map_sampler", ResourceKind::Sampler, 2, 2},
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
            {"g_buffer_sampler", ResourceKind::Sampler, 1, 5},
            {"Vxgi", ResourceKind::UniformBuffer, 2, 0},
            {"voxel_tex", ResourceKind::TextureReadOnly, 2, 2},
            {"voxel_tex_mipmap", ResourceKind::TextureReadOnly, 2, 3, 6},
            {"voxel_sampler", ResourceKind::Sampler, 2, 9},
        }
    );

    require_shader_resources(
        "deferred_gi_composite.frag",
        {
            {"g_position_ao", ResourceKind::TextureReadOnly, 1, 0},
            {"g_normal_roughness", ResourceKind::TextureReadOnly, 1, 1},
            {"g_emissive_depth", ResourceKind::TextureReadOnly, 1, 4},
            {"g_buffer_sampler", ResourceKind::Sampler, 1, 5},
            {"direct_lighting", ResourceKind::TextureReadOnly, 2, 0},
            {"indirect_lighting", ResourceKind::TextureReadOnly, 2, 1},
            {"composite_sampler", ResourceKind::Sampler, 2, 2},
        }
    );

    require_shader_resources(
        "deferred_present.frag",
        {
            {"composite", ResourceKind::TextureReadOnly, 0, 0},
            {"composite_sampler", ResourceKind::Sampler, 0, 1},
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
            {"sampler", ResourceKind::Sampler, 2, 7},
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
            {"shadow_map_sampler", ResourceKind::Sampler, 2, 2},
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
            {"voxel_sampler", ResourceKind::Sampler, 0, 10},
        }
    );
}
