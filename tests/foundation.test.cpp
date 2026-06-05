#include <catch2/catch_test_macros.hpp>

#include "asset/assets.hpp"
#include "asset/event.hpp"
#include "asset/io.hpp"
#include "asset/path.hpp"
#include "base/bitflags.hpp"
#include "base/optional.hpp"
#include "ecs/event.hpp"
#include "ecs/world.hpp"
#include "graphics/enums.hpp"
#include "math/common.hpp"
#include "math/matrix.hpp"
#include "math/vector.hpp"

#include <array>
#include <cstddef>
#include <string>

using namespace fei;

namespace {

struct FoundationAsset {
    int value {0};
};

} // namespace

TEST_CASE("Asset primitives parse paths and memory readers", "[asset][base]") {
    SECTION("AssetPath splits source and path") {
        AssetPath path("embeded://shaders/forward.frag");

        REQUIRE(path.source().has_value());
        REQUIRE(*path.source() == "embeded");
        REQUIRE(path.path().generic_string() == "shaders/forward.frag");
        REQUIRE(path.as_string() == "embeded://shaders/forward.frag");
    }

    SECTION("Reader exposes memory as bytes and strings") {
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
}

TEST_CASE("Assets emit queued events", "[asset][event]") {
    using Event = AssetEvent<FoundationAsset>;

    World world;
    world.add_resource(Events<Event> {});
    auto& assets = world.add_resource(Assets<FoundationAsset>(nullptr));

    auto handle = assets.emplace(FoundationAsset {.value = 7});
    auto asset = assets.get(handle);

    REQUIRE(asset.has_value());
    REQUIRE(asset->value == 7);

    world.run_system_once(Assets<FoundationAsset>::track_assets);

    std::size_t last_event = 0;
    EventReader<Event> reader(world.resource<Events<Event>>(), last_event);

    auto added = reader.next();
    REQUIRE(added.has_value());
    REQUIRE(added->type == AssetEventType::Added);
    REQUIRE(added->id == handle.id());

    auto modified = reader.next();
    REQUIRE(modified.has_value());
    REQUIRE(modified->type == AssetEventType::Modified);
    REQUIRE(modified->id == handle.id());

    REQUIRE_FALSE(reader.next().has_value());
}

TEST_CASE("Base helpers keep optional and flag semantics", "[base]") {
    SECTION("Optional stores and resets values") {
        Optional<int> value;
        REQUIRE_FALSE(value.has_value());
        REQUIRE(value.value_or(5) == 5);

        value = 42;
        REQUIRE(value.has_value());
        REQUIRE(*value == 42);
        REQUIRE(value.value_or(5) == 42);

        value.reset();
        REQUIRE_FALSE(value.has_value());
    }

    SECTION("BitFlags set and unset enum flags") {
        BitFlags<TextureUsage> usage {
            TextureUsage::Sampled,
            TextureUsage::Storage,
        };

        REQUIRE(usage.is_set(TextureUsage::Sampled));
        REQUIRE(usage.is_set(TextureUsage::Storage));
        REQUIRE_FALSE(usage.is_set(TextureUsage::RenderTarget));

        usage.unset(TextureUsage::Storage);
        REQUIRE(usage.is_set(TextureUsage::Sampled));
        REQUIRE_FALSE(usage.is_set(TextureUsage::Storage));
    }
}

TEST_CASE("Math primitives compute deterministic results", "[math]") {
    SECTION("Vector arithmetic") {
        Vector3 x_axis {1.0f, 0.0f, 0.0f};
        Vector3 y_axis {0.0f, 1.0f, 0.0f};

        auto cross = Vector3::cross(x_axis, y_axis);

        REQUIRE(Vector3::dot(x_axis, y_axis) == 0.0f);
        REQUIRE(cross.x == 0.0f);
        REQUIRE(cross.y == 0.0f);
        REQUIRE(cross.z == 1.0f);
    }

    SECTION("Matrix transforms") {
        auto transform = translate(1.0f, 2.0f, 3.0f) *
                         scale(2.0f, 3.0f, 4.0f);
        auto result = transform * Vector4 {1.0f, 1.0f, 1.0f, 1.0f};

        REQUIRE(result.x == 3.0f);
        REQUIRE(result.y == 5.0f);
        REQUIRE(result.z == 7.0f);
        REQUIRE(result.w == 1.0f);
    }
}
