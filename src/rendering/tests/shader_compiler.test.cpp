#include "rendering/shader_compiler.hpp"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace fei;

namespace {

ShaderCompileRequest test_slang_request() {
    return ShaderCompileRequest {
        .language = ShaderSourceLanguage::Slang,
        .source_path = "src/pbr/shaders/forward.slang",
        .source_root = "src/pbr/shaders",
        .logical_path = "forward.frag",
        .output_root = "build/runtime/shaders",
        .dependency_path = "forward.slang.fragment",
        .stage = ShaderStages::Fragment,
        .entry = "fragment_main",
        .defs = {
            ShaderDefVal::uint_def("LIGHT_COUNT", 4),
            ShaderDefVal::bool_def("ALPHA_TEST"),
        },
    };
}

ShaderCompileTools test_tools() {
    return ShaderCompileTools {
        .glslc = "glslc",
        .slangc = "slangc",
        .shadergen = "fei-shadergen",
    };
}

bool contains_arg(
    const std::vector<std::string>& args,
    const std::string& expected
) {
    return std::find(args.begin(), args.end(), expected) != args.end();
}

std::filesystem::path
write_text_file(const std::filesystem::path& path, std::string content) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary);
    output << content;
    return path;
}

std::string read_text_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return std::string(
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>()
    );
}

} // namespace

TEST_CASE(
    "shader compile artifact paths use logical shader paths",
    "[rendering][shader-compiler]"
) {
    auto artifacts = shader_compile_artifact_paths(test_slang_request());

    REQUIRE(
        artifacts.spirv_path.generic_string() ==
        "build/runtime/shaders/vulkan/forward.frag.spv"
    );
    REQUIRE(
        artifacts.opengl_path.generic_string() ==
        "build/runtime/shaders/opengl/forward.frag"
    );
    REQUIRE(
        artifacts.reflection_path.generic_string() ==
        "build/runtime/shaders/reflection/forward.frag.json"
    );
    REQUIRE(
        artifacts.slang_reflection_path.generic_string() ==
        "build/runtime/shaders/reflection/forward.slang.fragment.json"
    );
    REQUIRE(
        artifacts.depfile_path.generic_string() ==
        "build/runtime/shaders/deps/forward.slang.fragment.mk.d"
    );
}

TEST_CASE(
    "shader compile plan passes normalized defs to slangc",
    "[rendering][shader-compiler]"
) {
    auto plan = make_shader_compile_plan(test_slang_request(), test_tools());

    REQUIRE(plan.has_value());
    REQUIRE(plan->invocations.size() == 2);
    REQUIRE(plan->invocations[0].program == "slangc");
    REQUIRE(plan->invocations[1].program == "fei-shadergen");

    const auto& slang_args = plan->invocations[0].args;
    REQUIRE(contains_arg(slang_args, "-target"));
    REQUIRE(contains_arg(slang_args, "spirv"));
    REQUIRE(contains_arg(slang_args, "-entry"));
    REQUIRE(contains_arg(slang_args, "fragment_main"));
    REQUIRE(contains_arg(slang_args, "-stage"));
    REQUIRE(contains_arg(slang_args, "fragment"));
    REQUIRE(contains_arg(slang_args, "-DALPHA_TEST=1"));
    REQUIRE(contains_arg(slang_args, "-DLIGHT_COUNT=4"));

    auto alpha_define =
        std::find(slang_args.begin(), slang_args.end(), "-DALPHA_TEST=1");
    auto light_define =
        std::find(slang_args.begin(), slang_args.end(), "-DLIGHT_COUNT=4");
    REQUIRE(alpha_define < light_define);
}

TEST_CASE(
    "ExternalShaderCompiler runs planned invocations in order",
    "[rendering][shader-compiler]"
) {
    std::vector<std::filesystem::path> programs;
    ExternalShaderCompiler compiler(
        test_tools(),
        [&](const ShaderCompilerInvocation& invocation)
            -> Status<ShaderCompileError> {
            programs.push_back(invocation.program);
            return {};
        }
    );

    auto output = compiler.compile(test_slang_request());

    REQUIRE(output.has_value());
    REQUIRE(
        programs == std::vector<std::filesystem::path> {
                        "slangc",
                        "fei-shadergen",
                    }
    );
    REQUIRE(
        output->artifacts.spirv_path.generic_string() ==
        "build/runtime/shaders/vulkan/forward.frag.spv"
    );
}

TEST_CASE(
    "ExternalShaderCompiler returns runner failures",
    "[rendering][shader-compiler]"
) {
    ExternalShaderCompiler compiler(
        test_tools(),
        [](const ShaderCompilerInvocation&) -> Status<ShaderCompileError> {
            return failure(
                ShaderCompileError {
                    .message = "compile failed",
                    .diagnostics = "syntax error",
                }
            );
        }
    );

    auto output = compiler.compile(test_slang_request());

    REQUIRE_FALSE(output.has_value());
    REQUIRE(output.error().message == "compile failed");
    REQUIRE(output.error().diagnostics == "syntax error");
}

#ifdef FEI_HAS_SLANG_SDK
TEST_CASE(
    "SlangLibraryShaderCompiler compiles Slang with in-process shadergen",
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
        .language = ShaderSourceLanguage::Slang,
        .source_path = source_path,
        .source_root = root,
        .logical_path = "shader.frag",
        .output_root = root / "out",
        .dependency_path = "shader.slang.fragment",
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
    REQUIRE(std::filesystem::exists(output->artifacts.spirv_path));
    REQUIRE(std::filesystem::file_size(output->artifacts.spirv_path) > 0);
    REQUIRE(std::filesystem::exists(output->artifacts.opengl_path));
    REQUIRE(std::filesystem::file_size(output->artifacts.opengl_path) > 0);
    REQUIRE(std::filesystem::exists(output->artifacts.reflection_path));
    REQUIRE(read_text_file(output->artifacts.reflection_path)
                .contains("entryPoints"));
    REQUIRE(std::filesystem::exists(output->artifacts.slang_reflection_path));
    REQUIRE(read_text_file(output->artifacts.slang_reflection_path)
                .contains("\"name\": \"Material\""));
    REQUIRE(read_text_file(output->artifacts.reflection_path)
                .contains("\"name\" : \"Material\""));
}
#endif
