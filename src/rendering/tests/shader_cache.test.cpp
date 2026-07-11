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
#include <functional>
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

std::string read_text_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return std::string(
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>()
    );
}

class RecordingShaderCompiler final : public ShaderCompiler {
  public:
    std::vector<ShaderCompileRequest> requests;
    std::vector<std::filesystem::path> dependencies;
    std::vector<ShaderResourceBinding> resources;
    std::function<void()> after_dependency_snapshot;

    [[nodiscard]] std::string cache_identity() const override {
        return "recording-shader-compiler-v1";
    }

    Result<ShaderCompileOutput, ShaderCompileError>
    compile(ShaderCompileRequest request) override {
        requests.push_back(request);
        std::vector<ShaderDependencySnapshot> dependency_snapshots;
        dependency_snapshots.reserve(dependencies.size());
        for (const auto& dependency : dependencies) {
            dependency_snapshots.push_back(
                ShaderDependencySnapshot {
                    .path = dependency,
                    .source = read_text_file(dependency),
                }
            );
        }
        if (after_dependency_snapshot) {
            after_dependency_snapshot();
        }

        return ShaderCompileOutput {
            .description =
                ShaderDescription {
                    .stage = request.stage,
                    .source = "#version 450\nvoid main() {}\n",
                    .spirv =
                        {
                            std::byte {0x03},
                            std::byte {0x02},
                            std::byte {0x23},
                            std::byte {0x07},
                        },
                    .path = request.logical_path.string(),
                    .resources = resources,
                    .defs = normalized_shader_defs(std::move(request.defs)),
                },
            .dependencies = dependencies,
            .dependency_snapshots = std::move(dependency_snapshots),
        };
    }
};

Handle<Shader> add_test_shader(Assets<Shader>& shaders) {
    return shaders.add(
        std::make_unique<Shader>(Shader {
            .path = "test.slang",
            .source = "test shader source",
        })
    );
}

void add_shader_asset_loading(
    App& app,
    const std::filesystem::path& source_root
) {
    app.add_resource(AssetServer(&app));
    app.resource<AssetServer>().emplace_source<ShaderAssetSource>(source_root);
    app.resource<AssetServer>().add_loader<Shader, ShaderLoader>();
}

} // namespace

TEST_CASE(
    "ShaderCache reuses shader modules for equivalent variant defs",
    "[rendering][shader-cache]"
) {
    AssetServer asset_server(nullptr);
    Assets<Shader> shaders(nullptr);
    FakeGraphicsDevice device;
    RecordingShaderCompiler compiler;
    ShaderVariantCompiler variant_compiler(compiler);
    ShaderCache cache(asset_server, shaders, device, &variant_compiler);
    auto shader = add_test_shader(shaders);

    auto first = cache.get(
        shader.id(),
        ShaderStages::Fragment,
        {},
        {
            ShaderDefVal::bool_def("ALPHA_TEST"),
            ShaderDefVal::uint_def("LIGHT_COUNT", 4),
        }
    );
    auto second = cache.get(
        shader.id(),
        ShaderStages::Fragment,
        {},
        {
            ShaderDefVal::uint_def("LIGHT_COUNT", 4),
            ShaderDefVal::bool_def("ALPHA_TEST"),
        }
    );

    REQUIRE(first == second);
    REQUIRE(compiler.requests.size() == 1);
    REQUIRE(compiler.requests[0].source == "test shader source");
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
    RecordingShaderCompiler compiler;
    ShaderVariantCompiler variant_compiler(compiler);
    ShaderCache cache(asset_server, shaders, device, &variant_compiler);
    auto shader = add_test_shader(shaders);

    auto without_defs = cache.get(shader.id(), ShaderStages::Fragment, {});
    auto alpha_test = cache.get(
        shader.id(),
        ShaderStages::Fragment,
        {},
        {ShaderDefVal::bool_def("ALPHA_TEST")}
    );
    auto alpha_test_again = cache.get(
        shader.id(),
        ShaderStages::Fragment,
        {},
        {ShaderDefVal::bool_def("ALPHA_TEST")}
    );
    auto alpha_test_false = cache.get(
        shader.id(),
        ShaderStages::Fragment,
        {},
        {ShaderDefVal::bool_def("ALPHA_TEST", false)}
    );

    REQUIRE(without_defs != alpha_test);
    REQUIRE(alpha_test == alpha_test_again);
    REQUIRE(alpha_test != alpha_test_false);
    REQUIRE(compiler.requests.size() == 3);
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
        }
    );

    AssetServer asset_server(nullptr);
    Assets<Shader> shaders(nullptr);
    FakeGraphicsDevice device;
    ShaderCache cache(asset_server, shaders, device, &variant_compiler);
    auto shader = add_test_shader(shaders);

    auto first = cache.get(
        shader.id(),
        ShaderStages::Fragment,
        {},
        {
            ShaderDefVal::uint_def("LIGHT_COUNT", 4),
            ShaderDefVal::bool_def("ALPHA_TEST"),
        }
    );
    auto second = cache.get(
        shader.id(),
        ShaderStages::Fragment,
        {},
        {
            ShaderDefVal::bool_def("ALPHA_TEST"),
            ShaderDefVal::uint_def("LIGHT_COUNT", 4),
        }
    );

    REQUIRE(first == second);
    REQUIRE(compiler.requests.size() == 1);
    REQUIRE(
        compiler.requests[0].logical_path == std::filesystem::path("test.slang")
    );
    REQUIRE(
        compiler.requests[0].source_path == root / "shaders" / "test.slang"
    );
    REQUIRE(compiler.requests[0].source == "test shader source");
    REQUIRE(compiler.requests[0].stage == ShaderStages::Fragment);
    REQUIRE(compiler.requests[0].entry == "fragment_main");
    REQUIRE(
        compiler.requests[0].defs ==
        normalized_shader_defs({
            ShaderDefVal::bool_def("ALPHA_TEST"),
            ShaderDefVal::uint_def("LIGHT_COUNT", 4),
        })
    );
    REQUIRE(device.shader_descriptions.size() == 1);
    REQUIRE(device.shader_descriptions[0].path == "test.slang");
    REQUIRE(
        device.shader_descriptions[0].defs ==
        normalized_shader_defs({
            ShaderDefVal::bool_def("ALPHA_TEST"),
            ShaderDefVal::uint_def("LIGHT_COUNT", 4),
        })
    );
}

TEST_CASE(
    "ShaderCache compiles runtime Slang shader assets",
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
        }
    );

    App app;
    add_shader_asset_loading(app, root / "shaders");
    auto& asset_server = app.resource<AssetServer>();
    auto& shaders = app.resource<Assets<Shader>>();
    FakeGraphicsDevice device;
    ShaderCache cache(asset_server, shaders, device, &variant_compiler);

    auto default_shader = cache.get_or_compile(
        AssetPath("shader://test.slang"),
        ShaderStages::Fragment,
        {}
    );
    auto default_shader_again = cache.get_or_compile(
        AssetPath("shader://test.slang"),
        ShaderStages::Fragment,
        {}
    );
    auto alpha_test = cache.get_or_compile(
        AssetPath("shader://test.slang"),
        ShaderStages::Fragment,
        {},
        {ShaderDefVal::bool_def("ALPHA_TEST")}
    );
    auto alpha_test_again = cache.get_or_compile(
        AssetPath("shader://test.slang"),
        ShaderStages::Fragment,
        {},
        {ShaderDefVal::bool_def("ALPHA_TEST")}
    );
    auto alpha_test_from_shader_ref = cache.get(
        ShaderRef("shader://test.slang"),
        ShaderStages::Fragment,
        {},
        {ShaderDefVal::bool_def("ALPHA_TEST")}
    );
    auto default_shader_from_shader_ref =
        cache.get(ShaderRef("shader://test.slang"), ShaderStages::Fragment, {});

    REQUIRE(default_shader == default_shader_again);
    REQUIRE(default_shader == default_shader_from_shader_ref);
    REQUIRE(alpha_test == alpha_test_again);
    REQUIRE(alpha_test == alpha_test_from_shader_ref);
    REQUIRE(default_shader != alpha_test);
    REQUIRE(compiler.requests.size() == 2);
    REQUIRE(compiler.requests[0].defs.empty());
    REQUIRE(
        compiler.requests[0].logical_path == std::filesystem::path("test.slang")
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
    "ShaderCache recompiles runtime Slang shader assets when compiler "
    "dependencies change",
    "[rendering][shader-cache][shader-compiler]"
) {
    auto root = std::filesystem::current_path() / "build" / "test" /
                "shader-cache-source-dependency-invalidation";
    std::filesystem::remove_all(root);
    auto dependency_path = root / "shaders" / "shared.slang";
    write_text_file(dependency_path, "first dependency content");
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
    compiler.dependencies = {dependency_path};
    ShaderVariantCompiler variant_compiler(
        compiler,
        RuntimeShaderCompilerConfig {
            .source_root = root / "shaders",
        }
    );

    App app;
    add_shader_asset_loading(app, root / "shaders");
    auto& asset_server = app.resource<AssetServer>();
    auto& shaders = app.resource<Assets<Shader>>();
    FakeGraphicsDevice device;
    ShaderCache cache(asset_server, shaders, device, &variant_compiler);

    auto first = cache.get_or_compile(
        AssetPath("shader://test.slang"),
        ShaderStages::Fragment,
        {}
    );
    auto first_again = cache.get_or_compile(
        AssetPath("shader://test.slang"),
        ShaderStages::Fragment,
        {}
    );

    REQUIRE(first == first_again);
    REQUIRE(compiler.requests.size() == 1);
    REQUIRE(device.shader_descriptions.size() == 1);

    write_text_file(dependency_path, "changed dependency content");
    std::filesystem::last_write_time(
        dependency_path,
        std::filesystem::file_time_type::clock::now() + std::chrono::seconds(2)
    );

    auto recompiled = cache.get_or_compile(
        AssetPath("shader://test.slang"),
        ShaderStages::Fragment,
        {}
    );

    REQUIRE(recompiled != first);
    REQUIRE(compiler.requests.size() == 2);
    REQUIRE(device.shader_descriptions.size() == 2);
}

TEST_CASE(
    "ShaderVariantCompiler persists compiled variants between instances",
    "[rendering][shader-cache][shader-compiler]"
) {
    auto root = std::filesystem::current_path() / "build" / "test" /
                "shader-cache-persistent";
    std::filesystem::remove_all(root);
    auto dependency_path = root / "shaders" / "shared.slang";
    write_text_file(dependency_path, "first dependency content");
    write_text_file(root / "shaders" / "test.slang", "shader source");

    auto config = RuntimeShaderCompilerConfig {
        .source_root = root / "shaders",
        .cache_root = root / "cache",
    };

    RecordingShaderCompiler first_compiler;
    first_compiler.dependencies = {dependency_path};
    first_compiler.resources = {
        ShaderResourceBinding {
            .name = "material",
            .backend_name = "material_1",
            .backend_names = {"material_1", "material_2"},
            .kind = ResourceKind::StorageBufferReadOnly,
            .set = 1,
            .binding = 2,
            .array_size = 2,
        },
    };
    ShaderVariantCompiler first_variant_compiler(first_compiler, config);
    auto first = first_variant_compiler.compile_with_dependencies(
        "test.slang",
        "shader source",
        ShaderStages::Fragment,
        "fragment_main",
        {
            ShaderDefVal::bool_def("ALPHA_TEST"),
            ShaderDefVal::int_def("OFFSET", -2),
            ShaderDefVal::uint_def("LIGHT_COUNT", 4),
        }
    );

    REQUIRE(first.has_value());
    REQUIRE(first_compiler.requests.size() == 1);

    RecordingShaderCompiler second_compiler;
    second_compiler.dependencies = {dependency_path};
    ShaderVariantCompiler second_variant_compiler(second_compiler, config);
    auto cached = second_variant_compiler.compile_with_dependencies(
        "test.slang",
        "shader source",
        ShaderStages::Fragment,
        "fragment_main",
        {
            ShaderDefVal::uint_def("LIGHT_COUNT", 4),
            ShaderDefVal::bool_def("ALPHA_TEST"),
            ShaderDefVal::int_def("OFFSET", -2),
        }
    );

    REQUIRE(cached.has_value());
    REQUIRE(second_compiler.requests.empty());
    CHECK(cached->description.source == first->description.source);
    CHECK(cached->description.spirv == first->description.spirv);
    CHECK(cached->description.defs == first->description.defs);
    REQUIRE(cached->description.resources.size() == 1);
    CHECK(cached->description.resources[0].name == "material");
    CHECK(cached->description.resources[0].backend_name == "material_1");
    CHECK(
        cached->description.resources[0].backend_names ==
        std::vector<std::string> {"material_1", "material_2"}
    );
    CHECK(
        cached->description.resources[0].kind ==
        ResourceKind::StorageBufferReadOnly
    );
    CHECK(cached->description.resources[0].set == 1);
    CHECK(cached->description.resources[0].binding == 2);
    CHECK(cached->description.resources[0].array_size == 2);

    write_text_file(dependency_path, "changed dependency content");

    RecordingShaderCompiler third_compiler;
    third_compiler.dependencies = {dependency_path};
    ShaderVariantCompiler third_variant_compiler(third_compiler, config);
    auto recompiled = third_variant_compiler.compile_with_dependencies(
        "test.slang",
        "shader source",
        ShaderStages::Fragment,
        "fragment_main",
        {
            ShaderDefVal::bool_def("ALPHA_TEST"),
            ShaderDefVal::int_def("OFFSET", -2),
            ShaderDefVal::uint_def("LIGHT_COUNT", 4),
        }
    );

    REQUIRE(recompiled.has_value());
    REQUIRE(third_compiler.requests.size() == 1);
}

TEST_CASE(
    "ShaderVariantCompiler skips cache writes when dependencies change during "
    "compilation",
    "[rendering][shader-cache][shader-compiler]"
) {
    auto root = std::filesystem::current_path() / "build" / "test" /
                "shader-cache-concurrent-dependency-change";
    std::filesystem::remove_all(root);
    auto dependency_path = root / "shaders" / "shared.slang";
    write_text_file(dependency_path, "dependency before compilation");
    write_text_file(root / "shaders" / "test.slang", "shader source");

    auto config = RuntimeShaderCompilerConfig {
        .source_root = root / "shaders",
        .cache_root = root / "cache",
    };

    RecordingShaderCompiler first_compiler;
    first_compiler.dependencies = {dependency_path};
    first_compiler.after_dependency_snapshot = [&] {
        write_text_file(dependency_path, "dependency changed during compile");
    };
    ShaderVariantCompiler first_variant_compiler(first_compiler, config);
    auto first = first_variant_compiler.compile_with_dependencies(
        "test.slang",
        "shader source",
        ShaderStages::Fragment,
        "fragment_main",
        {}
    );

    REQUIRE(first.has_value());
    REQUIRE(first_compiler.requests.size() == 1);

    RecordingShaderCompiler second_compiler;
    second_compiler.dependencies = {dependency_path};
    ShaderVariantCompiler second_variant_compiler(second_compiler, config);
    auto second = second_variant_compiler.compile_with_dependencies(
        "test.slang",
        "shader source",
        ShaderStages::Fragment,
        "fragment_main",
        {}
    );

    REQUIRE(second.has_value());
    REQUIRE(second_compiler.requests.size() == 1);
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
        }
    );

    auto output = variant_compiler.compile(
        "test.frag",
        ShaderStages::Fragment,
        {},
        {ShaderDefVal::bool_def("ALPHA_TEST")}
    );

    REQUIRE_FALSE(output.has_value());
    REQUIRE(output.error().message.contains("Runtime Slang shader source"));
    REQUIRE(output.error().message.contains("test.frag.slang"));
    REQUIRE(output.error().message.contains("test.slang"));
    REQUIRE(compiler.requests.empty());
}

TEST_CASE(
    "ShaderVariantCompiler resolves registered shader source prefixes",
    "[rendering][shader-cache][shader-compiler]"
) {
    auto root = std::filesystem::current_path() / "build" / "test" /
                "shader-cache-runtime-prefix";
    std::filesystem::remove_all(root);
    write_text_file(
        root / "pbr" / "test.slang",
        R"(
[shader("fragment")]
float4 fragment_main() : SV_Target0
{
    return float4(1.0, 0.0, 0.0, 1.0);
}
)"
    );

    ShaderSourceRegistry registry;
    registry.add_root("pbr", root / "pbr");

    RecordingShaderCompiler compiler;
    ShaderVariantCompiler variant_compiler(
        compiler,
        RuntimeShaderCompilerConfig {
            .shader_sources = registry,
        }
    );

    auto output = variant_compiler.compile(
        "pbr/test.slang",
        ShaderStages::Fragment,
        {},
        {ShaderDefVal::bool_def("ALPHA_TEST")}
    );

    REQUIRE(output.has_value());
    REQUIRE(compiler.requests.size() == 1);
    CHECK(
        compiler.requests[0].logical_path ==
        std::filesystem::path("pbr/test.slang")
    );
    CHECK(compiler.requests[0].source_path == root / "pbr" / "test.slang");
    CHECK(compiler.requests[0].source_root == root / "pbr");
    REQUIRE(compiler.requests[0].search_roots.size() == 1);
    CHECK(compiler.requests[0].search_roots[0] == root / "pbr");
}
