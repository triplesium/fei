#include "rendering/shader_cache.hpp"

#include "asset/assets.hpp"
#include "asset/server.hpp"
#include "graphics/enums.hpp"
#include "rendering/shader.hpp"
#include "test_graphics_device.hpp"

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string_view>
#include <vector>

using namespace fei;
using namespace fei::rendering_test;

namespace {

void write_text_file(
    const std::filesystem::path& path,
    std::string_view content
) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary);
    output << content;
}

void write_binary_file(
    const std::filesystem::path& path,
    const std::vector<std::byte>& bytes
) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary);
    output.write(
        reinterpret_cast<const char*>(bytes.data()),
        static_cast<std::streamsize>(bytes.size())
    );
}

class RecordingShaderCompiler final : public ShaderCompiler {
  public:
    std::vector<ShaderCompileRequest> requests;

    Result<ShaderCompileOutput, ShaderCompileError>
    compile(ShaderCompileRequest request) override {
        requests.push_back(request);

        auto artifacts = shader_compile_artifact_paths(request);
        write_text_file(
            artifacts.opengl_path,
            "#version 450\nvoid main() {}\n"
        );
        write_binary_file(
            artifacts.spirv_path,
            {
                std::byte {0x03},
                std::byte {0x02},
                std::byte {0x23},
                std::byte {0x07},
            }
        );
        write_text_file(
            artifacts.reflection_path,
            R"({"entryPoints":[{"name":"fragment_main","mode":"frag"}]})"
        );

        return ShaderCompileOutput {.artifacts = std::move(artifacts)};
    }
};

Handle<Shader> add_test_shader(Assets<Shader>& shaders) {
    return shaders.add(
        std::make_unique<Shader>(Shader {
            .path = "test.frag",
            .source = {},
            .spirv = {},
            .stage = ShaderStages::Fragment,
            .resources = {},
        })
    );
}

} // namespace

TEST_CASE(
    "ShaderCache reuses shader modules for equivalent variant defs",
    "[rendering][shader-cache]"
) {
    AssetServer asset_server(nullptr);
    Assets<Shader> shaders(nullptr);
    FakeGraphicsDevice device;
    ShaderCache cache(asset_server, shaders, device);
    auto shader = add_test_shader(shaders);

    auto first = cache.get(
        shader.id(),
        {
            ShaderDefVal::bool_def("ALPHA_TEST"),
            ShaderDefVal::uint_def("LIGHT_COUNT", 4),
        }
    );
    auto second = cache.get(
        shader.id(),
        {
            ShaderDefVal::uint_def("LIGHT_COUNT", 4),
            ShaderDefVal::bool_def("ALPHA_TEST"),
        }
    );

    REQUIRE(first == second);
    REQUIRE(device.shader_descriptions.size() == 1);
    REQUIRE(
        device.shader_descriptions[0].defs ==
        normalized_shader_defs({
            ShaderDefVal::bool_def("ALPHA_TEST"),
            ShaderDefVal::uint_def("LIGHT_COUNT", 4),
        })
    );
}

TEST_CASE(
    "ShaderCache separates shader modules for distinct variant defs",
    "[rendering][shader-cache]"
) {
    AssetServer asset_server(nullptr);
    Assets<Shader> shaders(nullptr);
    FakeGraphicsDevice device;
    ShaderCache cache(asset_server, shaders, device);
    auto shader = add_test_shader(shaders);

    auto without_defs = cache.get(shader.id());
    auto alpha_test =
        cache.get(shader.id(), {ShaderDefVal::bool_def("ALPHA_TEST")});
    auto alpha_test_again =
        cache.get(shader.id(), {ShaderDefVal::bool_def("ALPHA_TEST")});
    auto alpha_test_false =
        cache.get(shader.id(), {ShaderDefVal::bool_def("ALPHA_TEST", false)});

    REQUIRE(without_defs != alpha_test);
    REQUIRE(alpha_test == alpha_test_again);
    REQUIRE(alpha_test != alpha_test_false);
    REQUIRE(device.shader_descriptions.size() == 3);
    REQUIRE(device.shader_descriptions[0].defs.empty());
    REQUIRE(
        device.shader_descriptions[1].defs ==
        ShaderDefs {ShaderDefVal::bool_def("ALPHA_TEST")}
    );
    REQUIRE(
        device.shader_descriptions[2].defs ==
        ShaderDefs {ShaderDefVal::bool_def("ALPHA_TEST", false)}
    );
}

TEST_CASE(
    "ShaderCache compiles shader variants on cache miss",
    "[rendering][shader-cache][shader-compiler]"
) {
    auto root = std::filesystem::current_path() / "build" / "test" /
                "shader-cache-runtime-variant";
    std::filesystem::remove_all(root);
    write_text_file(
        root / "shaders" / "test.slang",
        R"(
[shader("fragment")]
float4 fragment_main() : SV_Target0
{
    return float4(1.0, 0.0, 0.0, 1.0);
}
)"
    );

    RecordingShaderCompiler compiler;
    ShaderVariantCompiler variant_compiler(
        compiler,
        RuntimeShaderCompilerConfig {
            .source_root = root / "shaders",
            .output_root = root / "out",
        }
    );

    AssetServer asset_server(nullptr);
    Assets<Shader> shaders(nullptr);
    FakeGraphicsDevice device;
    ShaderCache cache(asset_server, shaders, device, &variant_compiler);
    auto shader = add_test_shader(shaders);

    auto first = cache.get(
        shader.id(),
        {
            ShaderDefVal::uint_def("LIGHT_COUNT", 4),
            ShaderDefVal::bool_def("ALPHA_TEST"),
        }
    );
    auto second = cache.get(
        shader.id(),
        {
            ShaderDefVal::bool_def("ALPHA_TEST"),
            ShaderDefVal::uint_def("LIGHT_COUNT", 4),
        }
    );

    REQUIRE(first == second);
    REQUIRE(compiler.requests.size() == 1);
    REQUIRE(
        compiler.requests[0].logical_path == std::filesystem::path("test.frag")
    );
    REQUIRE(
        compiler.requests[0].source_path == root / "shaders" / "test.slang"
    );
    REQUIRE(compiler.requests[0].stage == ShaderStages::Fragment);
    REQUIRE(compiler.requests[0].entry == "fragment_main");
    REQUIRE(
        compiler.requests[0].defs ==
        normalized_shader_defs({
            ShaderDefVal::bool_def("ALPHA_TEST"),
            ShaderDefVal::uint_def("LIGHT_COUNT", 4),
        })
    );
    REQUIRE(compiler.requests[0].output_root.parent_path() == root / "out");
    REQUIRE(device.shader_descriptions.size() == 1);
    REQUIRE(device.shader_descriptions[0].path == "test.frag");
    REQUIRE(
        device.shader_descriptions[0].defs ==
        normalized_shader_defs({
            ShaderDefVal::bool_def("ALPHA_TEST"),
            ShaderDefVal::uint_def("LIGHT_COUNT", 4),
        })
    );
}

TEST_CASE(
    "ShaderCache can compile source-only Slang shader paths",
    "[rendering][shader-cache][shader-compiler]"
) {
    auto root = std::filesystem::current_path() / "build" / "test" /
                "shader-cache-source-only";
    std::filesystem::remove_all(root);
    write_text_file(
        root / "shaders" / "test.slang",
        R"(
[shader("fragment")]
float4 fragment_main() : SV_Target0
{
    return float4(1.0, 0.0, 0.0, 1.0);
}
)"
    );

    RecordingShaderCompiler compiler;
    ShaderVariantCompiler variant_compiler(
        compiler,
        RuntimeShaderCompilerConfig {
            .source_root = root / "shaders",
            .output_root = root / "out",
        }
    );

    AssetServer asset_server(nullptr);
    Assets<Shader> shaders(nullptr);
    FakeGraphicsDevice device;
    ShaderCache cache(asset_server, shaders, device, &variant_compiler);

    auto default_shader = cache.get_or_compile(AssetPath("shader://test.frag"));
    auto default_shader_again =
        cache.get_or_compile(AssetPath("shader://test.frag"));
    auto alpha_test = cache.get_or_compile(
        AssetPath("shader://test.frag"),
        {ShaderDefVal::bool_def("ALPHA_TEST")}
    );
    auto alpha_test_again = cache.get_or_compile(
        AssetPath("shader://test.frag"),
        {ShaderDefVal::bool_def("ALPHA_TEST")}
    );
    auto alpha_test_from_shader_ref = cache.get(
        ShaderRef("shader://test.frag"),
        {ShaderDefVal::bool_def("ALPHA_TEST")}
    );

    REQUIRE(default_shader == default_shader_again);
    REQUIRE(alpha_test == alpha_test_again);
    REQUIRE(alpha_test == alpha_test_from_shader_ref);
    REQUIRE(default_shader != alpha_test);
    REQUIRE(compiler.requests.size() == 2);
    REQUIRE(compiler.requests[0].defs.empty());
    REQUIRE(
        compiler.requests[0].logical_path == std::filesystem::path("test.frag")
    );
    REQUIRE(
        compiler.requests[0].source_path == root / "shaders" / "test.slang"
    );
    REQUIRE(
        compiler.requests[1].defs ==
        ShaderDefs {ShaderDefVal::bool_def("ALPHA_TEST")}
    );
    REQUIRE(device.shader_descriptions.size() == 2);
    REQUIRE(device.shader_descriptions[0].defs.empty());
    REQUIRE(
        device.shader_descriptions[1].defs ==
        ShaderDefs {ShaderDefVal::bool_def("ALPHA_TEST")}
    );
}

TEST_CASE(
    "ShaderCache recompiles source-only Slang shader paths when includes "
    "change",
    "[rendering][shader-cache][shader-compiler]"
) {
    auto root = std::filesystem::current_path() / "build" / "test" /
                "shader-cache-source-include-invalidation";
    std::filesystem::remove_all(root);
    auto include_path = root / "shaders" / "common.slangh";
    write_text_file(
        include_path,
        R"(
float4 test_color()
{
    return float4(1.0, 0.0, 0.0, 1.0);
}
)"
    );
    write_text_file(
        root / "shaders" / "test.slang",
        R"(
#include "common.slangh"

[shader("fragment")]
float4 fragment_main() : SV_Target0
{
    return test_color();
}
)"
    );

    RecordingShaderCompiler compiler;
    ShaderVariantCompiler variant_compiler(
        compiler,
        RuntimeShaderCompilerConfig {
            .source_root = root / "shaders",
            .output_root = root / "out",
        }
    );

    AssetServer asset_server(nullptr);
    Assets<Shader> shaders(nullptr);
    FakeGraphicsDevice device;
    ShaderCache cache(asset_server, shaders, device, &variant_compiler);

    auto first = cache.get_or_compile(AssetPath("shader://test.frag"));
    auto first_again = cache.get_or_compile(AssetPath("shader://test.frag"));

    REQUIRE(first == first_again);
    REQUIRE(compiler.requests.size() == 1);
    REQUIRE(device.shader_descriptions.size() == 1);

    write_text_file(
        include_path,
        R"(
float4 test_color()
{
    return float4(0.0, 1.0, 0.0, 1.0);
}
)"
    );
    std::filesystem::last_write_time(
        include_path,
        std::filesystem::file_time_type::clock::now() + std::chrono::seconds(2)
    );

    auto recompiled = cache.get_or_compile(AssetPath("shader://test.frag"));

    REQUIRE(recompiled != first);
    REQUIRE(compiler.requests.size() == 2);
    REQUIRE(device.shader_descriptions.size() == 2);
}

TEST_CASE(
    "ShaderVariantCompiler only resolves Slang source files",
    "[rendering][shader-cache][shader-compiler]"
) {
    auto root = std::filesystem::current_path() / "build" / "test" /
                "shader-cache-runtime-slang-only";
    std::filesystem::remove_all(root);
    write_text_file(
        root / "shaders" / "test.frag",
        "#version 450\nvoid main() {}\n"
    );

    RecordingShaderCompiler compiler;
    ShaderVariantCompiler variant_compiler(
        compiler,
        RuntimeShaderCompilerConfig {
            .source_root = root / "shaders",
            .output_root = root / "out",
        }
    );

    auto output = variant_compiler.compile(
        "test.frag",
        {ShaderDefVal::bool_def("ALPHA_TEST")}
    );

    REQUIRE_FALSE(output.has_value());
    REQUIRE(output.error().message.contains("Runtime Slang shader source"));
    REQUIRE(output.error().message.contains("test.frag.slang"));
    REQUIRE(output.error().message.contains("test.slang"));
    REQUIRE(compiler.requests.empty());
}
