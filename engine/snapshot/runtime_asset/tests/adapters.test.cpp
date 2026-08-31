#include "snapshot_runtime_asset/adapters.hpp"

#include "app/app.hpp"
#include "asset/assets.hpp"
#include "asset/database.hpp"
#include "asset/event.hpp"
#include "asset/loader.hpp"
#include "asset/server.hpp"
#include "asset/source.hpp"
#include "ecs/event.hpp"
#include "refl/cls.hpp"
#include "refl/registry.hpp"
#include "snapshot/world_snapshot.hpp"
#include "task/plugin.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>

using namespace ets;

namespace {

struct TestAsset {
    std::size_t byte_count {};
    std::string path;
};

struct AssetHolder {
    Handle<TestAsset> asset;
};

struct UntypedAssetHolder {
    UntypedHandle asset;
};

class MemorySource : public AssetSource {
  private:
    std::array<std::byte, 4> m_first {
        std::byte {1},
        std::byte {2},
        std::byte {3},
        std::byte {4},
    };
    std::array<std::byte, 2> m_second {
        std::byte {5},
        std::byte {6},
    };

  public:
    std::string name() const override { return "memory"; }

    bool exists(const std::filesystem::path& path) const override {
        const auto value = path.generic_string();
        return value == "first.bin" || value == "second.bin";
    }

    Result<Reader, std::string>
    try_get_reader(const std::filesystem::path& path) const override {
        const auto value = path.generic_string();
        if (value == "first.bin") {
            return Reader(m_first.data(), m_first.size());
        }
        if (value == "second.bin") {
            return Reader(m_second.data(), m_second.size());
        }
        return failure("memory asset not found: " + value);
    }
};

class ProjectSource : public AssetSource {
  private:
    std::array<std::byte, 3> m_bytes {
        std::byte {7},
        std::byte {8},
        std::byte {9},
    };

  public:
    std::string name() const override { return "project"; }

    bool exists(const std::filesystem::path& path) const override {
        const auto value = path.generic_string();
        return value == "old.bin" || value == "moved/new.bin";
    }

    Result<Reader, std::string>
    try_get_reader(const std::filesystem::path& path) const override {
        if (!exists(path)) {
            return failure("project asset not found: " + path.generic_string());
        }
        return Reader(m_bytes.data(), m_bytes.size());
    }
};

class TestLoader : public AssetLoader<TestAsset> {
  public:
    static inline std::atomic<int> load_count {0};

    AssetLoadResult<TestAsset>
    load(Reader& reader, const LoadContext& context) override {
        ++load_count;
        return std::make_unique<TestAsset>(TestAsset {
            .byte_count = reader.size(),
            .path = context.asset_path().as_string(),
        });
    }
};

void register_test_types() {
    static const bool registered = [] {
        Registry::instance()
            .register_cls<AssetHolder>(
                {"snapshot_runtime_asset_test"},
                "AssetHolder"
            )
            .add_property("asset", &AssetHolder::asset);
        Registry::instance()
            .register_cls<UntypedAssetHolder>(
                {"snapshot_runtime_asset_test"},
                "UntypedAssetHolder"
            )
            .add_property("asset", &UntypedAssetHolder::asset);
        return true;
    }();
    (void)registered;
}

void setup_memory_app(App& app) {
    register_test_types();
    app.add_plugin<TaskPlugin>();
    app.finish();
    AssetServer server(&app, "memory");
    server.emplace_source<MemorySource>();
    app.add_resource(std::move(server));
    app.resource<AssetServer>().add_loader<TestAsset, TestLoader>();
    app.world().sort_systems();
}

void setup_project_app(App& app, AssetUuid id, const AssetPath& path) {
    register_test_types();
    app.add_plugin<TaskPlugin>();
    app.finish();
    app.add_resource(AssetDatabase(std::filesystem::current_path()));
    REQUIRE(app.resource<AssetDatabase>().register_metadata(
        path,
        AssetMetadata {
            .id = id,
            .importer = "test",
            .settings = {},
        }
    ));
    AssetServer server(&app, "project");
    server.emplace_source<ProjectSource>();
    app.add_resource(std::move(server));
    app.resource<AssetServer>().add_loader<TestAsset, TestLoader>();
    app.world().sort_systems();
}

void configure_snapshots(World& world, snapshot::SnapshotRegistry& registry) {
    REQUIRE(snapshot_runtime_asset::configure_asset_adapters(world, registry));
    registry.resource<AppStates>(snapshot::ResourcePolicy::Ignore);
    registry.resource<CommandsQueue>(snapshot::ResourcePolicy::Ignore);
}

const snapshot::SnapshotAuditEntry*
component_audit(const snapshot::SnapshotAudit& audit, TypeId type) {
    const auto found =
        std::ranges::find(audit.components, type, [](const auto& entry) {
            return entry.type;
        });
    return found == audit.components.end() ? nullptr : &*found;
}

} // namespace

TEST_CASE(
    "Asset snapshot adapter discovers caches and restores handles",
    "[snapshot][asset][cache]"
) {
    TestLoader::load_count = 0;
    App app;
    setup_memory_app(app);
    auto& server = app.resource<AssetServer>();
    const auto first = server.load<TestAsset>("memory://first.bin");
    const auto entity = app.world().entity();
    app.world().add_component(entity, AssetHolder {.asset = first});
    app.world().add_component(
        entity,
        UntypedAssetHolder {.asset = first.untyped()}
    );

    snapshot::CheckpointStore checkpoints;
    configure_snapshots(app.world(), checkpoints.registry());
    const auto server_policy =
        checkpoints.registry().resource_policy(type_id<AssetServer>());
    const auto cache_policy =
        checkpoints.registry().resource_policy(type_id<Assets<TestAsset>>());
    const auto event_policy = checkpoints.registry().resource_policy(
        type_id<Events<AssetEvent<TestAsset>>>()
    );
    REQUIRE(server_policy);
    REQUIRE(cache_policy);
    REQUIRE(event_policy);
    CHECK(*server_policy == snapshot::ResourcePolicy::Ignore);
    CHECK(*cache_policy == snapshot::ResourcePolicy::Ignore);
    CHECK(*event_policy == snapshot::ResourcePolicy::Ignore);
    CHECK(
        checkpoints.registry().codecs().find(type_id<Handle<TestAsset>>()) !=
        nullptr
    );
    CHECK(
        checkpoints.registry().codecs().find(type_id<UntypedHandle>()) !=
        nullptr
    );
    const auto coverage = snapshot::audit(app.world(), checkpoints.registry());
    REQUIRE(coverage.ready);
    REQUIRE(coverage.complete);
    REQUIRE(checkpoints.create("assets", app.world(), true));

    const auto second = server.load<TestAsset>("memory://second.bin");
    app.world().get_component_rw<AssetHolder>(entity).write().asset = second;
    app.world().get_component_rw<UntypedAssetHolder>(entity).write().asset =
        second.untyped();
    REQUIRE(TestLoader::load_count == 2);

    auto restored = checkpoints.restore("assets", app.world());
    REQUIRE(restored);
    const auto restored_entity = restored->entity(1);
    REQUIRE(app.world().has_entity(restored_entity));
    const auto& restored_holder =
        app.world().get_component<AssetHolder>(restored_entity);
    const auto& restored_untyped =
        app.world().get_component<UntypedAssetHolder>(restored_entity);
    CHECK(restored_holder.asset.id() == first.id());
    CHECK(restored_untyped.asset.is<TestAsset>());
    CHECK(restored_untyped.asset.id() == first.id());
    CHECK(TestLoader::load_count == 2);
    REQUIRE(app.resource<Assets<TestAsset>>().get(restored_holder.asset));
    REQUIRE(app.resource<Assets<TestAsset>>().get(second));
}

TEST_CASE(
    "Asset snapshot adapter reconnects handles to pending async loads",
    "[snapshot][asset][async]"
) {
    TestLoader::load_count = 0;
    App app;
    setup_memory_app(app);
    auto handle =
        app.resource<AssetServer>().load_async<TestAsset>("memory://first.bin");
    const auto entity = app.world().entity();
    app.world().add_component(entity, AssetHolder {.asset = handle});
    const auto loading_state =
        app.resource<Assets<TestAsset>>().load_state(handle);
    REQUIRE(loading_state);
    REQUIRE(*loading_state == AssetLoadState::Loading);

    snapshot::CheckpointStore checkpoints;
    configure_snapshots(app.world(), checkpoints.registry());
    REQUIRE(checkpoints.create("loading", app.world(), true));
    app.world().get_component_rw<AssetHolder>(entity).write().asset = {};

    auto restored = checkpoints.restore("loading", app.world());
    REQUIRE(restored);
    const auto restored_handle =
        app.world().get_component<AssetHolder>(restored->entity(1)).asset;
    CHECK(restored_handle.id() == handle.id());

    for (int attempt = 0;
         attempt < 1000 &&
         !app.resource<Assets<TestAsset>>().get(restored_handle);
         ++attempt) {
        app.resource<Tasks>().drain_completions();
        app.run_schedule(PostUpdate);
        std::this_thread::sleep_for(std::chrono::milliseconds {1});
    }
    const auto loaded = app.resource<Assets<TestAsset>>().get(restored_handle);
    REQUIRE(loaded);
    CHECK(loaded->path == "memory://first.bin");
    CHECK(TestLoader::load_count == 1);
}

TEST_CASE(
    "Asset snapshot adapter rejects pathless runtime assets",
    "[snapshot][asset][audit]"
) {
    App app;
    setup_memory_app(app);
    const auto runtime_asset = app.resource<AssetServer>().add_asset(
        std::make_unique<TestAsset>(TestAsset {.byte_count = 9})
    );
    const auto entity = app.world().entity();
    app.world().add_component(entity, AssetHolder {.asset = runtime_asset});

    snapshot::CheckpointStore checkpoints;
    configure_snapshots(app.world(), checkpoints.registry());
    const auto coverage = snapshot::audit(app.world(), checkpoints.registry());
    REQUIRE_FALSE(coverage.ready);
    const auto* holder = component_audit(coverage, type_id<AssetHolder>());
    REQUIRE(holder != nullptr);
    CHECK_FALSE(holder->serializable);
    CHECK(holder->message.find("without a source path") != std::string::npos);

    const auto strict = checkpoints.create("runtime", app.world(), true);
    REQUIRE_FALSE(strict);
    CHECK(
        strict.error().kind == snapshot::SnapshotError::Kind::StrictAuditFailed
    );
    const auto permissive = checkpoints.create("runtime", app.world());
    REQUIRE_FALSE(permissive);
    CHECK(
        permissive.error().kind ==
        snapshot::SnapshotError::Kind::SerializeFailed
    );
}

TEST_CASE(
    "Asset snapshot adapter resolves moved project assets by UUID",
    "[snapshot][asset][uuid]"
) {
    TestLoader::load_count = 0;
    const auto id = AssetUuid::random();
    const AssetPath old_path("project://old.bin");
    const AssetPath moved_path("project://moved/new.bin");

    App save_app;
    setup_project_app(save_app, id, old_path);
    const auto saved_handle =
        save_app.resource<AssetServer>().load<TestAsset>(old_path);
    const auto entity = save_app.world().entity();
    save_app.world().add_component(entity, AssetHolder {.asset = saved_handle});
    snapshot::CheckpointStore checkpoints;
    configure_snapshots(save_app.world(), checkpoints.registry());
    REQUIRE(checkpoints.create("before-move", save_app.world(), true));

    REQUIRE(save_app.resource<AssetServer>().remove_path(old_path) == 1);
    AssetDatabase moved_database(std::filesystem::current_path());
    REQUIRE(moved_database.register_metadata(
        moved_path,
        AssetMetadata {
            .id = id,
            .importer = "test",
            .settings = {},
        }
    ));
    save_app.world().add_resource(std::move(moved_database));
    save_app.world().get_component_rw<AssetHolder>(entity).write().asset = {};

    auto restored = checkpoints.restore("before-move", save_app.world());
    REQUIRE(restored);
    const auto handle =
        save_app.world().get_component<AssetHolder>(restored->entity(1)).asset;
    const auto path = save_app.resource<Assets<TestAsset>>().path(handle);
    REQUIRE(path);
    CHECK(path->as_string() == moved_path.as_string());
    const auto loaded = save_app.resource<Assets<TestAsset>>().get(handle);
    REQUIRE(loaded);
    CHECK(loaded->path == moved_path.as_string());
    CHECK(TestLoader::load_count == 2);
}
