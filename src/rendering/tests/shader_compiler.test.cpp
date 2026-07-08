#include "rendering/shader_compiler.hpp"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

using namespace fei;

namespace {

std::filesystem::path
write_text_file(const std::filesystem::path& path, std::string content) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary);
    output << content;
    return path;
}

const ShaderResourceBinding&
require_resource(const ShaderDescription& shader, std::string_view name) {
    auto it = std::find_if(
        shader.resources.begin(),
        shader.resources.end(),
        [&](const ShaderResourceBinding& resource) {
            return resource.name == name;
        }
    );
    CAPTURE(name);
    REQUIRE(it != shader.resources.end());
    return *it;
}

} // namespace

#ifdef FEI_HAS_SLANG_SDK
TEST_CASE(
    "SlangLibraryShaderCompiler compiles Slang with in-process artifact "
    "generation",
    "[rendering][shader-compiler][slang]"
) {
    auto root = std::filesystem::current_path() / "build" / "test" /
                "slang-library-shader-compiler";
    std::filesystem::remove_all(root);
    auto source_path = root / "shader.slang";
    write_text_file(
        source_path,
        R"(
struct MaterialData
{
    float4 color;
};

layout(set = 2, binding = 0) ConstantBuffer<MaterialData> Material;

[shader("fragment")]
float4 fragment_main() : SV_Target0
{
#if ALPHA_TEST
    return Material.color + float4(float(LIGHT_COUNT) / 4.0, 0.0, 0.0, 0.0);
#else
    return Material.color;
#endif
}
)"
    );

    ShaderCompileRequest request {
        .source_path = source_path,
        .source_root = root,
        .logical_path = "shader.slang",
        .stage = ShaderStages::Fragment,
        .entry = "fragment_main",
        .defs = {
            ShaderDefVal::bool_def("ALPHA_TEST"),
            ShaderDefVal::uint_def("LIGHT_COUNT", 4),
        },
    };

    SlangLibraryShaderCompiler compiler;

    auto output = compiler.compile(request);

    if (!output) {
        INFO(output.error().message);
        INFO(output.error().diagnostics);
    }
    REQUIRE(output.has_value());
    REQUIRE(output->description.stage == ShaderStages::Fragment);
    REQUIRE(output->description.path == "shader.slang");
    REQUIRE_FALSE(output->description.source.empty());
    REQUIRE_FALSE(output->description.spirv.empty());
    REQUIRE(
        output->description.defs ==
        normalized_shader_defs({
            ShaderDefVal::bool_def("ALPHA_TEST"),
            ShaderDefVal::uint_def("LIGHT_COUNT", 4),
        })
    );
    REQUIRE(output->description.resources.size() == 1);
    REQUIRE(output->description.resources[0].name == "Material");
    REQUIRE(
        output->description.resources[0].kind == ResourceKind::UniformBuffer
    );
    REQUIRE(output->dependencies.size() == 1);
    REQUIRE(output->dependencies[0] == source_path.lexically_normal());
}

TEST_CASE(
    "SlangLibraryShaderCompiler maps ParameterBlock field resources to "
    "logical names",
    "[rendering][shader-compiler][slang]"
) {
    auto root = std::filesystem::current_path() / "build" / "test" /
                "slang-library-shader-compiler-parameter-block";
    std::filesystem::remove_all(root);
    auto source_path = root / "shader.slang";
    write_text_file(
        source_path,
        R"(
struct MaterialData
{
    float4 color;
};

struct MaterialBlock
{
    MaterialData data;
    Texture2D<float4> albedo_map;
    Texture2D<float4> normal_map;
    Texture2D<float4> metallic_map;
    Texture2D<float4> roughness_map;
    Texture2D<float4> emissive_map;
    Texture2D<float4> specular_map;
    SamplerState sampler;

    property float4 color
    {
        get { return data.color; }
    }
};

layout(set = 2) ParameterBlock<MaterialBlock> material;

[shader("fragment")]
float4 fragment_main() : SV_Target0
{
    return material.albedo_map.Sample(material.sampler, float2(0.5, 0.5)) *
           material.color;
}
)"
    );

    ShaderCompileRequest request {
        .source_path = source_path,
        .source_root = root,
        .logical_path = "shader.slang",
        .stage = ShaderStages::Fragment,
        .entry = "fragment_main",
    };

    SlangLibraryShaderCompiler compiler;

    auto output = compiler.compile(request);

    if (!output) {
        INFO(output.error().message);
        INFO(output.error().diagnostics);
    }
    REQUIRE(output.has_value());
    REQUIRE(output->description.resources.size() == 3);

    const auto& material = require_resource(output->description, "material");
    CHECK(material.kind == ResourceKind::UniformBuffer);
    CHECK(material.set == 2);
    CHECK(material.binding == 0);

    const auto& albedo = require_resource(output->description, "albedo_map");
    CHECK(albedo.kind == ResourceKind::TextureReadOnly);
    CHECK(albedo.set == 2);
    CHECK(albedo.binding == 1);

    const auto& material_sampler =
        require_resource(output->description, "sampler");
    CHECK(material_sampler.kind == ResourceKind::Sampler);
    CHECK(material_sampler.set == 2);
    CHECK(material_sampler.binding == 7);
}

TEST_CASE(
    "SlangLibraryShaderCompiler tracks imported Slang modules",
    "[rendering][shader-compiler][slang]"
) {
    auto root = std::filesystem::current_path() / "build" / "test" /
                "slang-library-shader-compiler-imports";
    std::filesystem::remove_all(root);
    auto source_path = root / "shader.slang";
    auto module_path = root / "test" / "colors.slang";
    auto module_include_path = root / "test" / "detail" / "colors.slangh";
    write_text_file(
        module_include_path,
        R"(
public float4 imported_color()
{
    return float4(1.0, 0.0, 0.0, 1.0);
}
)"
    );
    write_text_file(
        module_path,
        R"(
#include "detail/colors.slangh"
)"
    );
    write_text_file(
        source_path,
        R"(
import test.colors;

[shader("fragment")]
float4 fragment_main() : SV_Target0
{
    return imported_color();
}
)"
    );

    ShaderCompileRequest request {
        .source_path = source_path,
        .source_root = root,
        .logical_path = "shader.slang",
        .stage = ShaderStages::Fragment,
        .entry = "fragment_main",
    };

    SlangLibraryShaderCompiler compiler;

    auto output = compiler.compile(request);

    if (!output) {
        INFO(output.error().message);
        INFO(output.error().diagnostics);
    }
    REQUIRE(output.has_value());
    REQUIRE(output->description.stage == ShaderStages::Fragment);
    REQUIRE_FALSE(output->description.source.empty());
    REQUIRE_FALSE(output->description.spirv.empty());
    REQUIRE(
        std::find(
            output->dependencies.begin(),
            output->dependencies.end(),
            source_path.lexically_normal()
        ) != output->dependencies.end()
    );
    REQUIRE(
        std::find(
            output->dependencies.begin(),
            output->dependencies.end(),
            module_path.lexically_normal()
        ) != output->dependencies.end()
    );
    REQUIRE(
        std::find(
            output->dependencies.begin(),
            output->dependencies.end(),
            module_include_path.lexically_normal()
        ) != output->dependencies.end()
    );
}
#endif
