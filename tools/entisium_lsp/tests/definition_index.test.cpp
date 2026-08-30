#include "definition_index.hpp"

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
            ("entisium-lsp-definitions-" +
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
    std::ofstream output(path, std::ios::binary);
    output << content;
}

} // namespace

TEST_CASE(
    "definition index resolves files relative to itself",
    "[lsp][definitions]"
) {
    TemporaryDirectory temporary;
    write(temporary.path() / "globals.d.luau", "declare Example: number\n");
    write(
        temporary.path() / ".luaurc",
        R"({"aliases":{"entisium":"./modules"}})"
    );
    write(
        temporary.path() / "index.json",
        R"({
  "format":"entisium.luau-definitions",
  "version":1,
  "definitionFiles":{"@entisium":"globals.d.luau"},
  "baseLuaurc":".luaurc"
})"
    );

    const auto index =
        ets::lsp::load_definition_index(temporary.path() / "index.json");
    REQUIRE(index.definition_files.contains("@entisium"));
    CHECK(
        std::filesystem::path {index.definition_files.at("@entisium")} ==
        std::filesystem::absolute(temporary.path() / "globals.d.luau")
            .lexically_normal()
    );
    CHECK(index.base_config.has_value());
}
