#include "asset/io.hpp"
#include "asset/path.hpp"

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <unordered_map>

using namespace fei;

TEST_CASE(
    "AssetPath parses and hashes source-qualified paths",
    "[asset][path]"
) {
    AssetPath sourced("embeded://shaders/forward.frag");

    REQUIRE(sourced.source().has_value());
    REQUIRE(*sourced.source() == "embeded");
    REQUIRE(sourced.path().generic_string() == "shaders/forward.frag");
    REQUIRE(sourced.as_string() == "embeded://shaders/forward.frag");

    AssetPath default_source("textures/albedo.png");
    REQUIRE_FALSE(default_source.source().has_value());
    REQUIRE(default_source.path().generic_string() == "textures/albedo.png");
    REQUIRE(default_source.as_string() == "textures/albedo.png");

    std::unordered_map<AssetPath, int> values;
    values.emplace(sourced, 7);
    REQUIRE(values[AssetPath("embeded://shaders/forward.frag")] == 7);
}

TEST_CASE("Reader exposes memory as bytes and strings", "[asset][io]") {
    static constexpr std::array<std::byte, 5> bytes = {
        std::byte {'h'},
        std::byte {'e'},
        std::byte {'l'},
        std::byte {'l'},
        std::byte {'o'},
    };

    Reader reader(bytes.data(), bytes.size());

    REQUIRE(reader.size() == 5);
    REQUIRE(reader.data() == bytes.data());
    REQUIRE(reader.as_string_view() == "hello");
    REQUIRE(reader.as_string() == "hello");
}
