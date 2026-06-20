#include "asset/assets.hpp"

#include "asset/event.hpp"
#include "asset/io.hpp"
#include "asset/loader.hpp"
#include "asset/path.hpp"
#include "asset/server.hpp"
#include "ecs/event.hpp"
#include "ecs/world.hpp"

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <memory>

using namespace fei;

namespace {

struct TestAsset {
    int value {0};
};

class CountingLoader : public AssetLoader<TestAsset> {
  public:
    static inline int load_count = 0;

    AssetLoadResult<TestAsset>
    load(Reader& reader, const LoadContext&) override {
        ++load_count;
        return std::make_unique<TestAsset>(
            TestAsset {.value = static_cast<int>(reader.size())}
        );
    }
};

class FailingLoader : public AssetLoader<TestAsset> {
  public:
    AssetLoadResult<TestAsset>
    load(Reader&, const LoadContext& context) override {
        return failure(
            AssetLoadError(context.asset_path(), "test loader failed")
        );
    }
};

template<std::size_t Size>
Reader reader_for(const std::array<std::byte, Size>& bytes) {
    return Reader(bytes.data(), bytes.size());
}

} // namespace

TEST_CASE("Assets emit queued events", "[asset][event]") {
    using Event = AssetEvent<TestAsset>;

    World world;
    world.add_resource(Events<Event> {});
    auto& assets = world.add_resource(Assets<TestAsset>(nullptr));

    auto handle = assets.emplace(TestAsset {.value = 7});
    auto asset = assets.get(handle);

    REQUIRE(asset.has_value());
    REQUIRE(asset->value == 7);

    world.run_system_once(Assets<TestAsset>::track_assets);

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

TEST_CASE("Assets unload when the last handle is released", "[asset][handle]") {
    using Event = AssetEvent<TestAsset>;

    World world;
    world.add_resource(Events<Event> {});
    auto& assets = world.add_resource(Assets<TestAsset>(nullptr));

    AssetId id = 0;
    {
        auto handle = assets.emplace(TestAsset {.value = 9});
        id = handle.id();
        REQUIRE(assets.get(id).has_value());
    }

    REQUIRE_FALSE(assets.get(id).has_value());

    world.run_system_once(Assets<TestAsset>::track_assets);

    std::size_t last_event = 0;
    EventReader<Event> reader(world.resource<Events<Event>>(), last_event);

    REQUIRE(reader.next()->type == AssetEventType::Added);
    REQUIRE(reader.next()->type == AssetEventType::Modified);
    auto removed = reader.next();
    REQUIRE(removed.has_value());
    REQUIRE(removed->type == AssetEventType::Removed);
    REQUIRE(removed->id == id);
    REQUIRE_FALSE(reader.next().has_value());
}

TEST_CASE("Handle copies and moves keep assets alive", "[asset][handle]") {
    Assets<TestAsset> assets(nullptr);
    auto original = assets.emplace(TestAsset {.value = 3});
    auto id = original.id();

    {
        Handle<TestAsset> copied = original;
        Handle<TestAsset> moved = std::move(copied);
        REQUIRE(moved.id() == id);
    }

    auto asset = assets.get(id);
    REQUIRE(asset.has_value());
    REQUIRE(asset->value == 3);
}

TEST_CASE(
    "Assets load through loaders and reuse cached paths",
    "[asset][loader]"
) {
    CountingLoader::load_count = 0;

    App app;
    AssetServer server(&app);
    Assets<TestAsset> assets(std::make_unique<CountingLoader>());
    static constexpr std::array<std::byte, 3> first_bytes = {
        std::byte {1},
        std::byte {2},
        std::byte {3},
    };
    static constexpr std::array<std::byte, 5> second_bytes = {
        std::byte {1},
        std::byte {2},
        std::byte {3},
        std::byte {4},
        std::byte {5},
    };
    LoadContext context(server, AssetPath("memory://asset.bin"));

    auto first_reader = reader_for(first_bytes);
    auto first = assets.load(first_reader, context);
    auto second_reader = reader_for(second_bytes);
    auto second = assets.load(second_reader, context);

    REQUIRE(first.id() == second.id());
    REQUIRE(CountingLoader::load_count == 1);
    REQUIRE(assets.get(first)->value == 3);
}

TEST_CASE(
    "Assets reload stale cache entries after handles unload",
    "[asset][loader]"
) {
    CountingLoader::load_count = 0;

    App app;
    AssetServer server(&app);
    Assets<TestAsset> assets(std::make_unique<CountingLoader>());
    static constexpr std::array<std::byte, 1> first_bytes = {std::byte {1}};
    static constexpr std::array<std::byte, 2> second_bytes = {
        std::byte {1},
        std::byte {2},
    };
    LoadContext context(server, AssetPath("memory://asset.bin"));

    AssetId first_id = 0;
    {
        auto first_reader = reader_for(first_bytes);
        auto first = assets.load(first_reader, context);
        first_id = first.id();
    }

    auto second_reader = reader_for(second_bytes);
    auto second = assets.load(second_reader, context);

    REQUIRE(second.id() != first_id);
    REQUIRE(CountingLoader::load_count == 2);
    REQUIRE(assets.get(second)->value == 2);
}

TEST_CASE("Assets try_load returns loader errors", "[asset][loader]") {
    App app;
    AssetServer server(&app);
    Assets<TestAsset> assets(std::make_unique<FailingLoader>());
    static constexpr std::array<std::byte, 1> bytes = {std::byte {1}};
    LoadContext context(server, AssetPath("memory://asset.bin"));

    auto reader = reader_for(bytes);
    auto result = assets.try_load(reader, context);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error().path.as_string() == "memory://asset.bin");
    REQUIRE(result.error().message == "test loader failed");
}
