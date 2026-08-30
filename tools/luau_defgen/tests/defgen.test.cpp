#include "emitter.hpp"
#include "manifest.hpp"
#include "type_mapper.hpp"

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

class TemporaryDirectory {
  public:
    TemporaryDirectory() {
        m_path =
            std::filesystem::temp_directory_path() /
            ("entisium-luau-defgen-" +
             std::to_string(
                 std::chrono::steady_clock::now().time_since_epoch().count()
             ));
        std::filesystem::create_directories(m_path);
    }

    ~TemporaryDirectory() {
        std::error_code error;
        std::filesystem::remove_all(m_path, error);
    }

    [[nodiscard]] const std::filesystem::path& path() const { return m_path; }

  private:
    std::filesystem::path m_path;
};

void write(const std::filesystem::path& path, const std::string& content) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary);
    output << content;
}

[[nodiscard]] std::string read(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {
        std::istreambuf_iterator<char> {input},
        std::istreambuf_iterator<char> {},
    };
}

} // namespace

TEST_CASE(
    "defgen maps reflected and primitive C++ types",
    "[luau-defgen][types]"
) {
    ets::luau_defgen::Database database;
    database.classes.push_back({.cpp_name = "ets::Vector2", .name = "Vector2"});
    ets::luau_defgen::TypeMapper mapper {database};

    CHECK(mapper.map("bool") == "boolean");
    CHECK(mapper.map("const std::basic_string<char>&") == "string");
    CHECK(mapper.map("std::uint32_t") == "number");
    CHECK(mapper.map("const ets::Vector2&") == "Vector2");
    CHECK(mapper.map("const ets::Vector2*") == "Vector2?");
    CHECK(mapper.map("std::vector<float>") == "any");
    CHECK(mapper.unsupported_types().contains("std::vector<float>"));
}

TEST_CASE(
    "defgen emits global and native module definitions",
    "[luau-defgen][emit]"
) {
    TemporaryDirectory temporary;
    const auto manifest = temporary.path() / "vector.refl.json";
    const auto manual = temporary.path() / "runtime.d.luau";
    const auto output = temporary.path() / "out";
    write(manual, "export type entity = number\n");
    write(output / "modules" / "stale.luau", "return {}\n");
    write(
        manifest,
        R"({
  "format": "entisium.reflection",
  "version": 1,
  "module": "math",
  "annotationSchemas": [],
  "classes": [{
    "cppName": "ets::Vector2",
    "name": "Vector2",
    "namespace": ["ets"],
    "source": "engine/math/vector.hpp",
    "abstract": false,
    "annotations": [{"name": "ScriptPrelude", "arguments": []}],
    "properties": [{"name": "x", "cppType": "float"}],
    "methods": [{"name": "length", "returnCppType": "float", "parameters": [], "static": false, "const": true}],
    "constructors": [{"parameters": [{"name": "x", "cppType": "float"}]}]
  }],
  "enums": []
})"
    );

    const std::vector manifests {manifest};
    const auto database = ets::luau_defgen::load_manifests(manifests);
    const auto summary =
        ets::luau_defgen::emit_definitions(database, manual, output);

    CHECK(summary.class_count == 1);
    const auto globals = read(output / "globals.d.luau");
    CHECK(
        globals.find("declare extern type __Entisium_ets_Vector2") !=
        std::string::npos
    );
    CHECK(
        globals.find("export type Vector2 = __Entisium_ets_Vector2") !=
        std::string::npos
    );
    CHECK(globals.find("declare Vector2: {") != std::string::npos);
    CHECK(globals.find("new: ((x: number) -> Vector2)") != std::string::npos);
    CHECK(globals.find("__ets_type: Vector2?") != std::string::npos);
    const auto module = read(output / "modules" / "math.luau");
    CHECK(
        module.find("export type Vector2 = __Entisium_ets_Vector2") !=
        std::string::npos
    );
    CHECK(module.find("Vector2 = Vector2") != std::string::npos);
    CHECK(read(output / ".luaurc").find("\"entisium\"") != std::string::npos);
    CHECK_FALSE(std::filesystem::exists(output / "modules" / "stale.luau"));
}
