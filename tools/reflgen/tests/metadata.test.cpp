#include "../metadata.hpp"

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

class MetadataFixture {
  public:
    MetadataFixture() {
        const auto suffix =
            std::chrono::steady_clock::now().time_since_epoch().count();
        m_root = std::filesystem::temp_directory_path() /
                 ("entisium-reflgen-metadata-" + std::to_string(suffix));
        std::filesystem::create_directories(m_root);
    }

    ~MetadataFixture() { std::filesystem::remove_all(m_root); }

    [[nodiscard]] std::string write(std::string_view content) const {
        const auto file = m_root / "input.reflmeta";
        std::ofstream out(file, std::ios::binary);
        out << content;
        return file.generic_string();
    }

  private:
    std::filesystem::path m_root;
};

void require_validation_error(
    std::string_view metadata,
    std::initializer_list<std::string_view> fragments
) {
    const MetadataFixture fixture;
    try {
        ets::reflgen::validate_reflection_metadata({fixture.write(metadata)});
        FAIL("Expected reflection metadata validation to fail");
    } catch (const std::runtime_error& error) {
        const std::string message = error.what();
        for (const auto fragment : fragments) {
            REQUIRE(message.contains(fragment));
        }
    }
}

} // namespace

TEST_CASE(
    "Reflection metadata accepts typed annotation schemas",
    "[reflgen][metadata]"
) {
    const MetadataFixture fixture;
    const auto file = fixture.write(
        "schema \"Resource\" \"ets::annotations::Resource\" "
        "\"ecs/annotations.hpp\" 1 \"main_thread_only\" \"bool\"\n"
        "use \"Resource\" \"ets::Window\" \"window.hpp\" 1 "
        "\"main_thread_only\" \"true\"\n"
    );

    REQUIRE_NOTHROW(ets::reflgen::validate_reflection_metadata({file}));
}

TEST_CASE(
    "Reflection metadata rejects unknown annotations",
    "[reflgen][metadata]"
) {
    require_validation_error(
        "use \"Missing\" \"ets::Window\" \"window.hpp\" 0\n",
        {"Unknown annotation 'Missing'", "ets::Window", "window.hpp"}
    );
}

TEST_CASE(
    "Reflection metadata rejects unknown fields and invalid values",
    "[reflgen][metadata]"
) {
    require_validation_error(
        "schema \"Resource\" \"ets::annotations::Resource\" "
        "\"ecs/annotations.hpp\" 1 \"main_thread_only\" \"bool\"\n"
        "use \"Resource\" \"ets::Window\" \"window.hpp\" 1 "
        "\"thread_only\" \"true\"\n",
        {"Unknown field 'thread_only'", "Resource", "ets::Window"}
    );
    require_validation_error(
        "schema \"Resource\" \"ets::annotations::Resource\" "
        "\"ecs/annotations.hpp\" 1 \"main_thread_only\" \"bool\"\n"
        "use \"Resource\" \"ets::Window\" \"window.hpp\" 1 "
        "\"main_thread_only\" \"yes\"\n",
        {"Invalid bool value 'yes'", "Resource.main_thread_only"}
    );
}

TEST_CASE(
    "Reflection metadata rejects conflicting schemas",
    "[reflgen][metadata]"
) {
    require_validation_error(
        "schema \"Resource\" \"ets::annotations::Resource\" "
        "\"first.hpp\" 0\n"
        "schema \"Resource\" \"ets::annotations::OtherResource\" "
        "\"second.hpp\" 0\n",
        {"Annotation schema 'Resource'", "first.hpp", "second.hpp"}
    );
}
