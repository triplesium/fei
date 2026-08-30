#include "../manifest.hpp"

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <string>
#include <utility>

namespace {

class ManifestFixture {
  public:
    ManifestFixture() {
        const auto suffix =
            std::chrono::steady_clock::now().time_since_epoch().count();
        m_root = std::filesystem::temp_directory_path() /
                 ("entisium-reflgen-manifest-" + std::to_string(suffix));
        std::filesystem::create_directories(m_root);
    }

    ~ManifestFixture() { std::filesystem::remove_all(m_root); }

    [[nodiscard]] const std::filesystem::path& root() const { return m_root; }

    [[nodiscard]] std::filesystem::path source(std::string_view path) const {
        return m_root / path;
    }

    [[nodiscard]] std::filesystem::path output() const {
        return m_root / "generated/reflection.refl.json";
    }

    [[nodiscard]] nlohmann::json read() const {
        std::ifstream in(output(), std::ios::binary);
        return nlohmann::json::parse(in);
    }

  private:
    std::filesystem::path m_root;
};

ets::reflgen::MethodInfo method(
    std::string name,
    std::string return_type,
    std::string access = "public"
) {
    ets::reflgen::MethodInfo result;
    result.name = std::move(name);
    result.type_name = std::move(return_type);
    result.access = std::move(access);
    return result;
}

} // namespace

TEST_CASE(
    "Reflection manifest writes the registered reflection surface",
    "[reflgen][manifest]"
) {
    const ManifestFixture fixture;
    const auto source = fixture.source("include/types.hpp").generic_string();

    ets::reflgen::ParseResult result;
    result.annotation_schemas.push_back({
        .reflected_name = "Category",
        .type_name = "ets::annotations::Category",
        .source_file = source,
        .fields = {{
            .name = "name",
            .type_name = "std::string",
            .access = "public",
        }},
    });

    ets::reflgen::ClassInfo plugin {
        .name = "ets::ZedPlugin",
        .namespace_path = {"ets"},
        .local_name = "ZedPlugin",
        .source_file = source,
        .annotations =
            {
                {.name = "Plugin"},
                {
                    .name = "Category",
                    .arguments = {{.name = "name", .value = "tools\"debug"}},
                },
            },
        .properties = {
            {.name = "value", .type_name = "float", .access = "public"},
            {.name = "secret", .type_name = "int", .access = "private"},
        },
    };
    auto run = method("run", "bool");
    run.parameters.push_back({.name = "count", .type_name = "std::int32_t"});
    run.is_const = true;
    plugin.methods.push_back(std::move(run));
    plugin.methods.push_back(method("run", "void"));
    plugin.methods.push_back(method("setup", "void"));
    plugin.methods.push_back(method("hidden", "void", "private"));

    auto constructor = method("ets::ZedPlugin", "ets::ZedPlugin");
    constructor.parameters.push_back({.name = "value", .type_name = "float"});
    plugin.constructors.push_back(std::move(constructor));
    plugin.constructors.push_back(
        method("ets::ZedPlugin", "ets::ZedPlugin", "private")
    );
    result.classes.push_back(std::move(plugin));
    ets::reflgen::ClassInfo alpha {
        .name = "ets::Alpha",
        .namespace_path = {"ets"},
        .local_name = "Alpha",
        .source_file = source,
    };
    auto abstract_method = method("required", "void");
    abstract_method.is_abstract = true;
    alpha.methods.push_back(std::move(abstract_method));
    alpha.constructors.push_back(method("ets::Alpha", "ets::Alpha"));
    result.classes.push_back(std::move(alpha));
    result.classes.push_back({
        .name = "ets::Registry",
        .namespace_path = {"ets"},
        .local_name = "Registry",
        .source_file = source,
    });

    result.enums.push_back({
        .name = "ets::State",
        .namespace_path = {"ets"},
        .local_name = "State",
        .source_file = source,
        .annotations = {{.name = "ScriptPrelude"}},
        .underlying_type = "std::int32_t",
        .is_scoped = true,
        .values = {
            {.name = "Invalid", .value = -1},
            {.name = "Ready", .value = 1},
        },
    });

    ets::reflgen::write_reflection_manifest(
        result,
        fixture.root(),
        fixture.output(),
        "core"
    );
    const auto document = fixture.read();

    REQUIRE(document.at("format") == "entisium.reflection");
    REQUIRE(document.at("version") == 1);
    REQUIRE(document.at("module") == "core");
    REQUIRE(document.at("annotationSchemas").at(0).at("name") == "Category");
    REQUIRE(
        document.at("annotationSchemas").at(0).at("source") ==
        "include/types.hpp"
    );

    const auto& classes = document.at("classes");
    REQUIRE(classes.size() == 2);
    REQUIRE(classes.at(0).at("cppName") == "ets::Alpha");
    REQUIRE(classes.at(0).at("abstract") == true);
    REQUIRE(classes.at(0).at("constructors").empty());
    const auto& plugin_json = classes.at(1);
    REQUIRE(plugin_json.at("cppName") == "ets::ZedPlugin");
    REQUIRE(plugin_json.at("properties").size() == 1);
    REQUIRE(plugin_json.at("properties").at(0).at("name") == "value");
    REQUIRE(plugin_json.at("methods").size() == 2);
    REQUIRE(plugin_json.at("methods").at(0).at("name") == "run");
    REQUIRE(
        plugin_json.at("methods").at(0).at("parameters").at(0).at("cppType") ==
        "std::int32_t"
    );
    REQUIRE(plugin_json.at("constructors").size() == 1);
    REQUIRE(
        plugin_json.at("annotations").at(1).at("arguments").at(0).at("value") ==
        "tools\"debug"
    );

    const auto& enum_json = document.at("enums").at(0);
    REQUIRE(enum_json.at("underlyingCppType") == "std::int32_t");
    REQUIRE(enum_json.at("scoped") == true);
    REQUIRE(enum_json.at("values").at(0).at("value") == -1);
}

TEST_CASE(
    "Reflection manifest excludes codegen-unsupported members",
    "[reflgen][manifest]"
) {
    const ManifestFixture fixture;
    ets::reflgen::ParseResult result;
    result.classes.push_back({
        .name = "ets::Example",
        .namespace_path = {"ets"},
        .local_name = "Example",
        .source_file = fixture.source("example.hpp").generic_string(),
        .properties = {{
            .name = "unsupported",
            .type_name = "(anonymous struct)",
            .access = "public",
        }},
    });
    result.classes.front().methods.push_back(
        method("unsupported", "(anonymous struct)")
    );

    ets::reflgen::filter_codegen_unsupported_members(result);
    ets::reflgen::write_reflection_manifest(
        result,
        fixture.root(),
        fixture.output(),
        "example"
    );
    const auto document = fixture.read();
    const auto& class_json = document.at("classes").at(0);

    REQUIRE(class_json.at("properties").empty());
    REQUIRE(class_json.at("methods").empty());
}
