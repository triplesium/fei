#include "rendering/shader.hpp"

#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <utility>

using namespace fei;

namespace {

void write_text_file(
    const std::filesystem::path& path,
    std::string_view content
) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary);
    output << content;
}

} // namespace

TEST_CASE(
    "ShaderLoader loads runtime Slang shader source",
    "[rendering][shader]"
) {
    App app;
    AssetServer server(&app);
    SyncLoadContext context(server, AssetPath("shader://test.slang"));
    ShaderLoader loader;
    const std::string source = R"(
[shader("fragment")]
float4 fragment_main() : SV_Target0
{
    return float4(1.0, 0.0, 0.0, 1.0);
}
)";
    Reader reader(std::string_view(source.data(), source.size()));

    auto shader = loader.load(reader, context);

    REQUIRE(shader);
    REQUIRE((*shader)->path == std::filesystem::path("test.slang"));
    REQUIRE((*shader)->source == source);
}

TEST_CASE(
    "ShaderLoader rejects unsupported shader logical paths",
    "[rendering][shader]"
) {
    App app;
    AssetServer server(&app);
    SyncLoadContext context(server, AssetPath("shader://broken.txt"));
    ShaderLoader loader;
    Reader reader(std::string_view("shader source"));

    auto shader = loader.load(reader, context);

    REQUIRE_FALSE(shader);
    REQUIRE(shader.error().path.as_string() == "shader://broken.txt");
    REQUIRE(shader.error().message.contains("Unknown shader extension"));
}

TEST_CASE(
    "ShaderAssetSource reads runtime Slang source files",
    "[rendering][shader]"
) {
    auto root = std::filesystem::current_path() / "build" / "test" /
                "shader-source-reader";
    std::filesystem::remove_all(root);
    write_text_file(root / "shared.slang", "shared source");
    write_text_file(root / "specific.slang", "specific source");

    ShaderAssetSource source(root);

    REQUIRE(source.exists("shared.slang"));
    REQUIRE(source.exists("specific.slang"));
    REQUIRE_FALSE(source.exists("missing.slang"));
    REQUIRE_FALSE(source.exists("shared.txt"));

    auto shared_reader = source.try_get_reader("shared.slang");
    auto specific_reader = source.try_get_reader("specific.slang");

    REQUIRE(shared_reader);
    REQUIRE(specific_reader);
    REQUIRE(shared_reader->as_string_view() == "shared source");
    REQUIRE(specific_reader->as_string_view() == "specific source");
}

TEST_CASE(
    "ShaderAssetSource resolves registered source prefixes",
    "[rendering][shader]"
) {
    auto root = std::filesystem::current_path() / "build" / "test" /
                "shader-source-prefix-reader";
    std::filesystem::remove_all(root);
    write_text_file(root / "pbr" / "forward.slang", "pbr source");
    write_text_file(
        root / "modules" / "rendering" / "color.slang",
        "rendering source"
    );
    write_text_file(root / "ui" / "widget.slang", "ui source");

    ShaderSourceRegistry registry;
    registry.add_root("pbr", root / "pbr");
    registry.add_root("rendering", root / "modules");
    registry.add_root("ui", root / "ui");
    ShaderAssetSource source(std::move(registry));

    REQUIRE(source.exists("pbr/forward.slang"));
    REQUIRE(source.exists("rendering/color.slang"));
    REQUIRE(source.exists("ui/widget.slang"));
    REQUIRE_FALSE(source.exists("forward.slang"));
    REQUIRE_FALSE(source.exists("missing/forward.slang"));

    auto pbr_reader = source.try_get_reader("pbr/forward.slang");
    auto rendering_reader = source.try_get_reader("rendering/color.slang");
    auto ui_reader = source.try_get_reader("ui/widget.slang");

    REQUIRE(pbr_reader);
    REQUIRE(rendering_reader);
    REQUIRE(ui_reader);
    REQUIRE(pbr_reader->as_string_view() == "pbr source");
    REQUIRE(rendering_reader->as_string_view() == "rendering source");
    REQUIRE(ui_reader->as_string_view() == "ui source");
}
