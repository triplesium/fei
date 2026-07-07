#include "graphics/resource.hpp"
#include "rendering/shader.hpp"
#include "rendering/shader_compiler.hpp"

#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <string>
#include <string_view>
#include <unordered_map>
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

struct PbrShaderCase {
    std::string_view label;
    std::string_view source;
    ShaderStages stage;
};

std::filesystem::path pbr_shader_source_root() {
#ifdef FEI_SHADER_SOURCE_PATH
    return std::filesystem::path(FEI_SHADER_SOURCE_PATH);
#else
    return std::filesystem::current_path() / "src" / "pbr" / "shaders";
#endif
}

bool has_include_directive(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    REQUIRE(input);

    std::string line;
    while (std::getline(input, line)) {
        auto first = line.find_first_not_of(" \t");
        if (first != std::string::npos &&
            std::string_view(line).substr(first).starts_with("#include")) {
            return true;
        }
    }
    return false;
}

std::filesystem::path normalized_absolute_path(std::filesystem::path path) {
    std::error_code error;
    auto absolute = std::filesystem::absolute(path, error);
    if (!error) {
        path = std::move(absolute);
    }
    return path.lexically_normal();
}

constexpr std::array<PbrShaderCase, 25> PbrShaders {
    PbrShaderCase {
        "aniso_mipmapbase.comp",
        "aniso_mipmapbase.slang",
        ShaderStages::Compute
    },
    PbrShaderCase {
        "aniso_mipmapvolume.comp",
        "aniso_mipmapvolume.slang",
        ShaderStages::Compute
    },
    PbrShaderCase {"blur.frag", "blur.slang", ShaderStages::Fragment},
    PbrShaderCase {"color.frag", "color.slang", ShaderStages::Fragment},
    PbrShaderCase {
        "cubemap2irradiance.comp",
        "cubemap2irradiance.slang",
        ShaderStages::Compute
    },
    PbrShaderCase {
        "cubemap2radiance.comp",
        "cubemap2radiance.slang",
        ShaderStages::Compute
    },
    PbrShaderCase {
        "deferred_gi_composite.frag",
        "deferred_gi_composite.slang",
        ShaderStages::Fragment
    },
    PbrShaderCase {
        "deferred_gi_direct.frag",
        "deferred_gi_direct.slang",
        ShaderStages::Fragment
    },
    PbrShaderCase {
        "deferred_gi_indirect.frag",
        "deferred_gi_indirect.slang",
        ShaderStages::Fragment
    },
    PbrShaderCase {
        "deferred_prepass.frag",
        "deferred_prepass.slang",
        ShaderStages::Fragment
    },
    PbrShaderCase {
        "deferred_prepass.vert",
        "deferred_prepass.slang",
        ShaderStages::Vertex
    },
    PbrShaderCase {
        "deferred_present.frag",
        "deferred_present.slang",
        ShaderStages::Fragment
    },
    PbrShaderCase {
        "equirect2cube.comp",
        "equirect2cube.slang",
        ShaderStages::Compute
    },
    PbrShaderCase {"forward.frag", "forward.slang", ShaderStages::Fragment},
    PbrShaderCase {"forward.vert", "forward.slang", ShaderStages::Vertex},
    PbrShaderCase {
        "inject_propagation.comp",
        "inject_propagation.slang",
        ShaderStages::Compute
    },
    PbrShaderCase {
        "inject_radiance.comp",
        "inject_radiance.slang",
        ShaderStages::Compute
    },
    PbrShaderCase {"quad.vert", "quad.slang", ShaderStages::Vertex},
    PbrShaderCase {"shadow.frag", "shadow.slang", ShaderStages::Fragment},
    PbrShaderCase {"shadow.vert", "shadow.slang", ShaderStages::Vertex},
    PbrShaderCase {"skybox.frag", "skybox.slang", ShaderStages::Fragment},
    PbrShaderCase {"skybox.vert", "skybox.slang", ShaderStages::Vertex},
    PbrShaderCase {
        "voxelization.frag",
        "voxelization.slang",
        ShaderStages::Fragment
    },
    PbrShaderCase {
        "voxelization.geom",
        "voxelization.slang",
        ShaderStages::Geometry
    },
    PbrShaderCase {
        "voxelization.vert",
        "voxelization.slang",
        ShaderStages::Vertex
    },
};

#ifdef FEI_HAS_SLANG_SDK
PbrShaderCase pbr_shader_case(std::string_view shader_name) {
    auto it = std::find_if(
        PbrShaders.begin(),
        PbrShaders.end(),
        [&](const PbrShaderCase& shader) {
            return shader.label == shader_name;
        }
    );
    REQUIRE(it != PbrShaders.end());
    return *it;
}

const ShaderVariantCompileOutput&
compile_pbr_shader_output(std::string_view shader_name) {
    static SlangLibraryShaderCompiler compiler;
    static ShaderVariantCompiler variant_compiler(
        compiler,
        RuntimeShaderCompilerConfig {.source_root = pbr_shader_source_root()}
    );
    static std::unordered_map<std::string, ShaderVariantCompileOutput> cache;

    auto key = std::string(shader_name);
    auto it = cache.find(key);
    if (it != cache.end()) {
        return it->second;
    }

    auto shader = pbr_shader_case(shader_name);
    auto compiled = variant_compiler.compile_with_dependencies(
        std::filesystem::path(shader.source),
        shader.stage,
        std::string {},
        {}
    );
    CAPTURE(shader_name);
    if (!compiled) {
        INFO(compiled.error().message);
        INFO(compiled.error().diagnostics);
    }
    REQUIRE(compiled.has_value());

    auto [inserted, _] =
        cache.emplace(std::move(key), std::move(compiled).value());
    return inserted->second;
}

const ShaderDescription& compile_pbr_shader(std::string_view shader_name) {
    return compile_pbr_shader_output(shader_name).description;
}

std::vector<uint32> spirv_words(const ShaderDescription& shader) {
    REQUIRE(shader.spirv.size() % sizeof(uint32) == 0);

    std::vector<uint32> words(shader.spirv.size() / sizeof(uint32));
    std::memcpy(words.data(), shader.spirv.data(), shader.spirv.size());
    REQUIRE(words.size() >= 5);
    REQUIRE(words.front() == 0x07230203);
    return words;
}

bool has_spirv_execution_mode(
    const std::vector<uint32>& words,
    uint32 execution_mode
) {
    constexpr std::size_t header_word_count = 5;
    for (std::size_t index = header_word_count; index < words.size();) {
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
    const auto& bindings = compile_pbr_shader(shader_name).resources;

    CAPTURE(shader_name);
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

bool shader_has_dependency(
    const ShaderVariantCompileOutput& output,
    const std::filesystem::path& dependency
) {
    auto normalized = normalized_absolute_path(dependency);
    return std::find(
               output.dependencies.begin(),
               output.dependencies.end(),
               normalized
           ) != output.dependencies.end();
}

void require_shader_dependencies(
    std::string_view shader_name,
    std::initializer_list<std::string_view> dependencies
) {
    const auto& output = compile_pbr_shader_output(shader_name);
    for (auto dependency : dependencies) {
        auto path = pbr_shader_source_root() /
                    std::filesystem::path(std::string(dependency));
        CAPTURE(shader_name);
        CAPTURE(path);
        CHECK(shader_has_dependency(output, path));
    }
}
#endif

} // namespace

TEST_CASE(
    "PBR shader sources use Slang modules instead of includes",
    "[pbr][shader]"
) {
    auto root = pbr_shader_source_root();
    REQUIRE(std::filesystem::is_directory(root));

    bool found_shader_source = false;
    for (const auto& entry :
         std::filesystem::recursive_directory_iterator(root)) {
        if (!entry.is_regular_file()) {
            continue;
        }

        const auto& path = entry.path();
        auto extension = path.extension();
        if (extension != ".slang" && extension != ".slangh") {
            continue;
        }

        found_shader_source = true;
        CAPTURE(path);
        CHECK(extension == ".slang");
        CHECK_FALSE(has_include_directive(path));
    }

    CHECK(found_shader_source);
}

#ifdef FEI_HAS_SLANG_SDK
TEST_CASE("PBR shaders compile through runtime compiler", "[pbr][shader]") {
    for (auto shader_case : PbrShaders) {
        CAPTURE(shader_case.label);

        const auto& output = compile_pbr_shader_output(shader_case.label);
        const auto& shader = output.description;
        CHECK(shader.stage == shader_case.stage);
        CHECK(shader.path == shader_case.source);
        CHECK_FALSE(shader.source.empty());
        CHECK_FALSE(shader.spirv.empty());
        CHECK(shader_has_dependency(
            output,
            pbr_shader_source_root() /
                std::filesystem::path(std::string(shader_case.source))
        ));
        CHECK_FALSE(output.dependencies.empty());
        for (const auto& dependency : output.dependencies) {
            CAPTURE(shader_case.label);
            CAPTURE(dependency);
            CHECK(dependency.extension() == ".slang");
            CHECK(std::filesystem::is_regular_file(dependency));
        }
    }
}

TEST_CASE(
    "PBR shader dependencies include imported Slang modules",
    "[pbr][shader]"
) {
    require_shader_dependencies(
        "forward.frag",
        {
            "forward.slang",
            "forward/io.slang",
            "lib/brdf.slang",
            "lib/color.slang",
            "lib/constants.slang",
            "lib/normal.slang",
            "lib/view.slang",
            "material/types.slang",
            "mesh/types.slang",
            "mesh/vertex_input.slang",
        }
    );

    require_shader_dependencies(
        "deferred_gi_direct.frag",
        {
            "deferred_gi_direct.slang",
            "deferred/gbuffer.slang",
            "lib/brdf.slang",
            "lib/color.slang",
            "lib/constants.slang",
            "lib/fullscreen.slang",
            "lib/view.slang",
            "lighting/evsm.slang",
            "lighting/types.slang",
        }
    );

    require_shader_dependencies(
        "deferred_gi_indirect.frag",
        {
            "deferred_gi_indirect.slang",
            "deferred/gbuffer.slang",
            "lib/color.slang",
            "lib/constants.slang",
            "lib/fullscreen.slang",
            "lib/view.slang",
            "vxgi/basis.slang",
            "vxgi/types.slang",
        }
    );

    require_shader_dependencies(
        "inject_propagation.comp",
        {
            "inject_propagation.slang",
            "lib/constants.slang",
            "lib/normal.slang",
            "vxgi/basis.slang",
        }
    );
}

TEST_CASE(
    "PBR Vulkan shader outputs avoid unsupported pixel-center execution mode",
    "[pbr][shader]"
) {
    for (auto shader_case : PbrShaders) {
        CAPTURE(shader_case.label);
        CHECK_FALSE(has_spirv_execution_mode(
            spirv_words(compile_pbr_shader(shader_case.label)),
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
#else
TEST_CASE("PBR shader layout tests require Slang SDK", "[pbr][shader]") {
    SUCCEED("Slang SDK is not available in this build");
}
#endif
