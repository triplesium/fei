#include "asset/io.hpp"
#include "asset/path.hpp"

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <filesystem>
#include <unordered_map>

using namespace ets;

TEST_CASE(
    "AssetPath parses and hashes source-qualified paths",
    "[asset][path]"
) {
    AssetPath sourced("embedded://shaders/forward.frag");

    REQUIRE(sourced.source().has_value());
    REQUIRE(*sourced.source() == "embedded");
    REQUIRE(sourced.path().generic_string() == "shaders/forward.frag");
    REQUIRE(sourced.as_string() == "embedded://shaders/forward.frag");

    AssetPath default_source("textures/albedo.png");
    REQUIRE_FALSE(default_source.source().has_value());
    REQUIRE(default_source.path().generic_string() == "textures/albedo.png");
    REQUIRE(default_source.as_string() == "textures/albedo.png");

    std::unordered_map<AssetPath, int> values;
    values.emplace(sourced, 7);
    REQUIRE(values[AssetPath("embedded://shaders/forward.frag")] == 7);
}

TEST_CASE("AssetPath resolves virtual asset paths", "[asset][path]") {
    const AssetPath directory("package://models/robot");

    CHECK(
        directory.resolve_str("textures/./base.png") ==
        AssetPath("package://models/robot/textures/base.png")
    );
    CHECK(
        directory.resolve_str("../shared/base.png") ==
        AssetPath("package://models/shared/base.png")
    );
    CHECK(
        directory.resolve_str("/shared/base.png") ==
        AssetPath("package://shared/base.png")
    );
    CHECK(
        directory.resolve_str("other://shared/base.png") ==
        AssetPath("other://shared/base.png")
    );
}

TEST_CASE("AssetPath can be normalized and source-qualified", "[asset][path]") {
    const AssetPath path("textures/./ui/../player.png");

    CHECK(path.normalized() == AssetPath("textures/player.png"));
    CHECK(
        path.with_source("project") ==
        AssetPath("project://textures/player.png")
    );
}

TEST_CASE("AssetPath strings use portable separators", "[asset][path]") {
    const auto native = std::filesystem::path("textures") / "ui" / "button.png";
    const auto path = AssetPath(native).with_source("project");

    CHECK(path.as_string() == "project://textures/ui/button.png");
}

TEST_CASE(
    "AssetPath resolves references embedded in asset files",
    "[asset][path]"
) {
    const AssetPath document("package://models/robot/scene.gltf");

    CHECK(
        document.resolve_embed_str("textures/base.png") ==
        AssetPath("package://models/robot/textures/base.png")
    );
    CHECK(
        document.resolve_embed_str("../shared/model.bin") ==
        AssetPath("package://models/shared/model.bin")
    );
    CHECK(
        document.resolve_embed_str("/shared/model.bin") ==
        AssetPath("package://shared/model.bin")
    );
}

TEST_CASE("AssetPath identifies unapproved paths", "[asset][path]") {
    CHECK_FALSE(AssetPath("models/robot.bin").is_unapproved());
    CHECK_FALSE(AssetPath("models/robot/scene.gltf")
                    .resolve_embed_str("../../shared/model.bin")
                    .is_unapproved());
    CHECK(AssetPath("../secret.bin").is_unapproved());
    CHECK(AssetPath("models/../../secret.bin").is_unapproved());
    CHECK(AssetPath("C:/secret.bin").is_unapproved());
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
