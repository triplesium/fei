#include "asset/server.hpp"

#include "asset/assets.hpp"
#include "asset/io.hpp"
#include "asset/loader.hpp"
#include "asset/path.hpp"
#include "asset/plugin.hpp"
#include "asset/request.hpp"
#include "asset/source.hpp"
#include "ecs/event.hpp"
#include "task/plugin.hpp"

#include <array>
#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <future>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>

using namespace fei;
namespace {

struct DependencyAsset {
    int byte_count {0};
    std::string path;
};

struct ServerAsset {
    int byte_count {0};
    std::string path;
    Handle<DependencyAsset> dependency;
};

using ServerAssetLoadFn = decltype(&AssetServer::load<ServerAsset>);
using ServerAssetLoadAsyncFn = decltype(&AssetServer::load_async<ServerAsset>);

static_assert(
    std::is_invocable_v<ServerAssetLoadFn, AssetServer&, const AssetPath&>
);
static_assert(
    !std::
        is_invocable_v<ServerAssetLoadFn, const AssetServer&, const AssetPath&>
);
static_assert(
    std::is_invocable_v<ServerAssetLoadAsyncFn, AssetServer&, const AssetPath&>
);
static_assert(!std::is_invocable_v<
              ServerAssetLoadAsyncFn,
              const AssetServer&,
              const AssetPath&>);

class MemorySource : public AssetSource {
  private:
    std::array<std::byte, 4> m_bytes {
        std::byte {1},
        std::byte {2},
        std::byte {3},
        std::byte {4},
    };
    std::array<std::byte, 2> m_dependency_bytes {
        std::byte {5},
        std::byte {6},
    };

  public:
    std::string name() const override { return "memory"; }

    bool exists(const std::filesystem::path& path) const override {
        auto asset_path = path.generic_string();
        return asset_path == "asset.bin" || asset_path == "dependency.bin";
    }

    Result<Reader, std::string>
    try_get_reader(const std::filesystem::path& path) const override {
        if (path.generic_string() == "dependency.bin") {
            return Reader(m_dependency_bytes.data(), m_dependency_bytes.size());
        }
        if (path.generic_string() != "asset.bin") {
            return failure("memory asset not found: " + path.generic_string());
        }
        return Reader(m_bytes.data(), m_bytes.size());
    }
};

class FailingReadSource : public AssetSource {
  public:
    std::string name() const override { return "broken"; }

    bool exists(const std::filesystem::path& path) const override {
        return path.generic_string() == "asset.bin";
    }

    Result<Reader, std::string>
    try_get_reader(const std::filesystem::path& /*path*/) const override {
        return failure(std::string("source read failed"));
    }
};

class ServerLoader : public AssetLoader<ServerAsset> {
  public:
    static inline std::atomic<int> load_count = 0;

    AssetLoadResult<ServerAsset>
    load(Reader& reader, const LoadContext& context) override {
        ++load_count;
        return std::make_unique<ServerAsset>(ServerAsset {
            .byte_count = static_cast<int>(reader.size()),
            .path = context.asset_path().as_string(),
        });
    }
};

class DependencyLoader : public AssetLoader<DependencyAsset> {
  public:
    static inline std::atomic<int> load_count = 0;

    AssetLoadResult<DependencyAsset>
    load(Reader& reader, const LoadContext& context) override {
        ++load_count;
        return std::make_unique<DependencyAsset>(DependencyAsset {
            .byte_count = static_cast<int>(reader.size()),
            .path = context.asset_path().as_string(),
        });
    }
};

class FailingDependencyLoader : public AssetLoader<DependencyAsset> {
  public:
    AssetLoadResult<DependencyAsset>
    load(Reader&, const LoadContext& context) override {
        return failure(
            AssetLoadError(context.asset_path(), "dependency loader failed")
        );
    }
};

class DependentServerLoader : public AssetLoader<ServerAsset> {
  public:
    static inline std::atomic<int> load_count = 0;

    AssetLoadResult<ServerAsset>
    load(Reader& reader, const LoadContext& context) override {
        ++load_count;
        auto dependency =
            context.load<DependencyAsset>(AssetPath("memory://dependency.bin"));

        return std::make_unique<ServerAsset>(ServerAsset {
            .byte_count = static_cast<int>(reader.size()),
            .path = context.asset_path().as_string(),
            .dependency = std::move(dependency),
        });
    }
};

class FailingServerLoader : public AssetLoader<ServerAsset> {
  public:
    AssetLoadResult<ServerAsset>
    load(Reader&, const LoadContext& context) override {
        return failure(
            AssetLoadError(context.asset_path(), "server loader failed")
        );
    }
};

class ContextKindLoader : public AssetLoader<ServerAsset> {
  public:
    AssetLoadResult<ServerAsset>
    load(Reader& reader, const LoadContext& context) override {
        auto has_sync_context =
            dynamic_cast<const SyncLoadContext*>(&context) != nullptr;
        return std::make_unique<ServerAsset>(ServerAsset {
            .byte_count = has_sync_context ? 1 : 2,
            .path = context.asset_path().as_string(),
        });
    }
};

template<typename Done>
void run_post_update_until(App& app, Done done) {
    for (int i = 0; i < 1000 && !done(); ++i) {
        app.run_schedule(PostUpdate);
        std::this_thread::sleep_for(std::chrono::milliseconds {1});
    }
    REQUIRE(done());
}

} // namespace

TEST_CASE("AssetsPlugin installs task resources", "[asset][plugin]") {
    App app;
    app.add_plugin<AssetsPlugin>();

    REQUIRE(app.has_resource<AssetServer>());
    REQUIRE(app.has_resource<AssetLoadRequests>());
    REQUIRE(app.has_resource<Tasks>());
    REQUIRE(app.has_plugin<TaskPlugin>());
}

TEST_CASE(
    "AssetLoadRequests close cancels pending dependency requests",
    "[asset][server][async]"
) {
    AssetLoadRequests requests;
    auto result = std::async(std::launch::async, [&]() {
        return requests.load<ServerAsset>(AssetPath("memory://asset.bin"));
    });

    std::this_thread::sleep_for(std::chrono::milliseconds {1});
    requests.close();

    bool threw = false;
    try {
        (void)result.get();
    } catch (const std::runtime_error& error) {
        threw = true;
        REQUIRE(std::string(error.what()).contains("closed"));
    }
    REQUIRE(threw);
}

TEST_CASE(
    "AssetServer loads assets through registered sources and loaders",
    "[asset][server]"
) {
    App app;
    AssetServer server(&app);
    server.emplace_source<MemorySource>();
    app.add_resource(std::move(server));
    app.resource<AssetServer>().add_loader<ServerAsset, ServerLoader>();

    auto handle = app.resource<AssetServer>().load<ServerAsset>(
        AssetPath("memory://asset.bin")
    );
    auto& assets = app.resource<Assets<ServerAsset>>();
    auto asset = assets.get(handle);

    REQUIRE(asset.has_value());
    REQUIRE(asset->byte_count == 4);
    REQUIRE(asset->path == "memory://asset.bin");
}

TEST_CASE(
    "AssetServer records sync dependency load states",
    "[asset][server]"
) {
    App app;
    AssetServer server(&app);
    server.emplace_source<MemorySource>();
    app.add_resource(std::move(server));
    app.resource<AssetServer>()
        .add_loader<ServerAsset, DependentServerLoader>();
    app.resource<AssetServer>().add_loader<DependencyAsset, DependencyLoader>();

    auto handle = app.resource<AssetServer>().load<ServerAsset>(
        AssetPath("memory://asset.bin")
    );
    auto& server_resource = app.resource<AssetServer>();
    auto& assets = app.resource<Assets<ServerAsset>>();
    auto asset = assets.get(handle);

    REQUIRE(asset.has_value());
    REQUIRE(asset->dependency.id() != invalid_asset_id);

    auto dependencies = server_resource.dependencies(handle);
    REQUIRE(dependencies.size() == 1);
    REQUIRE(dependencies[0].type == type_id<DependencyAsset>());
    REQUIRE(dependencies[0].id == asset->dependency.id());
    REQUIRE(server_resource.is_loaded(handle));
    REQUIRE(
        server_resource.dependency_load_state(handle) == AssetLoadState::Loaded
    );
    REQUIRE(
        server_resource.recursive_dependency_load_state(handle) ==
        AssetLoadState::Loaded
    );
    REQUIRE(server_resource.is_loaded_with_dependencies(handle));
}

TEST_CASE("AssetServer load stores missing source errors", "[asset][server]") {
    App app;
    AssetServer server(&app);
    app.add_resource(std::move(server));
    app.resource<AssetServer>().add_loader<ServerAsset, ServerLoader>();

    auto handle = app.resource<AssetServer>().load<ServerAsset>(
        AssetPath("missing://asset.bin")
    );
    auto& assets = app.resource<Assets<ServerAsset>>();

    auto state = assets.load_state(handle);
    REQUIRE(state.has_value());
    REQUIRE(*state == AssetLoadState::Failed);
    auto error = assets.load_error(handle);
    REQUIRE(error.has_value());
    REQUIRE(error->path.as_string() == "missing://asset.bin");
    REQUIRE(error->message.contains("No asset source found"));
}

TEST_CASE("AssetServer load stores source reader errors", "[asset][server]") {
    App app;
    AssetServer server(&app);
    server.emplace_source<FailingReadSource>();
    app.add_resource(std::move(server));
    app.resource<AssetServer>().add_loader<ServerAsset, ServerLoader>();

    auto handle = app.resource<AssetServer>().load<ServerAsset>(
        AssetPath("broken://asset.bin")
    );
    auto& assets = app.resource<Assets<ServerAsset>>();

    auto state = assets.load_state(handle);
    REQUIRE(state.has_value());
    REQUIRE(*state == AssetLoadState::Failed);
    auto error = assets.load_error(handle);
    REQUIRE(error.has_value());
    REQUIRE(error->path.as_string() == "broken://asset.bin");
    REQUIRE(error->message.contains("source read failed"));
}

TEST_CASE(
    "AssetServer load_async passes read-only load contexts",
    "[asset][server][async]"
) {
    App app;
    app.add_plugin<TaskPlugin>();
    AssetServer server(&app);
    server.emplace_source<MemorySource>();
    app.add_resource(std::move(server));
    app.resource<AssetServer>().add_loader<ServerAsset, ContextKindLoader>();
    app.world().sort_systems();

    auto handle = app.resource<AssetServer>().load_async<ServerAsset>(
        AssetPath("memory://asset.bin")
    );
    auto& assets = app.resource<Assets<ServerAsset>>();
    REQUIRE(
        app.resource<AssetServer>().dependency_load_state(handle) ==
        AssetLoadState::Loading
    );
    REQUIRE_FALSE(
        app.resource<AssetServer>().is_loaded_with_dependencies(handle)
    );

    run_post_update_until(app, [&]() {
        return assets.get(handle).has_value();
    });

    auto asset = assets.get(handle);
    REQUIRE(asset.has_value());
    REQUIRE(asset->byte_count == 2);
}

TEST_CASE(
    "AssetServer load_async completes assets through task completions",
    "[asset][server][async]"
) {
    ServerLoader::load_count = 0;

    App app;
    app.add_plugin<TaskPlugin>();
    AssetServer server(&app);
    server.emplace_source<MemorySource>();
    app.add_resource(std::move(server));
    app.resource<AssetServer>().add_loader<ServerAsset, ServerLoader>();
    app.world().sort_systems();

    auto handle = app.resource<AssetServer>().load_async<ServerAsset>(
        AssetPath("memory://asset.bin")
    );
    auto& assets = app.resource<Assets<ServerAsset>>();

    auto state = assets.load_state(handle);
    REQUIRE(state.has_value());
    REQUIRE(*state == AssetLoadState::Loading);
    REQUIRE_FALSE(assets.get(handle).has_value());

    for (int i = 0; i < 1000 && ServerLoader::load_count.load() != 1; ++i) {
        app.resource<Tasks>().drain_completions();
        std::this_thread::sleep_for(std::chrono::milliseconds {1});
    }
    REQUIRE(ServerLoader::load_count.load() == 1);

    state = assets.load_state(handle);
    REQUIRE(state.has_value());
    REQUIRE(*state == AssetLoadState::Loading);
    REQUIRE_FALSE(assets.get(handle).has_value());

    run_post_update_until(app, [&]() {
        return assets.get(handle).has_value();
    });

    auto asset = assets.get(handle);
    REQUIRE(asset.has_value());
    REQUIRE(asset->byte_count == 4);
    REQUIRE(asset->path == "memory://asset.bin");
    state = assets.load_state(handle);
    REQUIRE(state.has_value());
    REQUIRE(*state == AssetLoadState::Loaded);
    REQUIRE(ServerLoader::load_count.load() == 1);
}

TEST_CASE(
    "AssetServer load_async lets loaders request dependencies",
    "[asset][server][async]"
) {
    DependentServerLoader::load_count = 0;
    DependencyLoader::load_count = 0;

    App app;
    app.add_plugin<AssetsPlugin>();
    auto& server = app.resource<AssetServer>();
    server.emplace_source<MemorySource>();
    server.add_loader<ServerAsset, DependentServerLoader>();
    server.add_loader<DependencyAsset, DependencyLoader>();
    app.world().sort_systems();

    auto handle = app.resource<AssetServer>().load_async<ServerAsset>(
        AssetPath("memory://asset.bin")
    );
    auto& assets = app.resource<Assets<ServerAsset>>();

    run_post_update_until(app, [&]() {
        return assets.get(handle).has_value();
    });

    AssetId dependency_id = invalid_asset_id;
    {
        auto asset = assets.get(handle);
        REQUIRE(asset.has_value());
        REQUIRE(asset->byte_count == 4);
        REQUIRE(asset->path == "memory://asset.bin");
        REQUIRE(asset->dependency.id() != invalid_asset_id);
        dependency_id = asset->dependency.id();
    }

    auto& dependencies = app.resource<Assets<DependencyAsset>>();
    auto state = dependencies.load_state(dependency_id);
    REQUIRE(state.has_value());
    auto recorded_dependencies =
        app.resource<AssetServer>().dependencies(handle);
    REQUIRE(recorded_dependencies.size() == 1);
    REQUIRE(recorded_dependencies[0].type == type_id<DependencyAsset>());
    REQUIRE(recorded_dependencies[0].id == dependency_id);

    run_post_update_until(app, [&]() {
        return dependencies.get(dependency_id).has_value();
    });

    auto dependency = dependencies.get(dependency_id);
    REQUIRE(dependency.has_value());
    REQUIRE(dependency->byte_count == 2);
    REQUIRE(dependency->path == "memory://dependency.bin");
    REQUIRE(DependentServerLoader::load_count.load() == 1);
    REQUIRE(DependencyLoader::load_count.load() == 1);
    REQUIRE(
        app.resource<AssetServer>().dependency_load_state(handle) ==
        AssetLoadState::Loaded
    );
    REQUIRE(
        app.resource<AssetServer>().recursive_dependency_load_state(handle) ==
        AssetLoadState::Loaded
    );
    REQUIRE(app.resource<AssetServer>().is_loaded_with_dependencies(handle));
}

TEST_CASE(
    "AssetServer reports async dependency load failures",
    "[asset][server][async]"
) {
    App app;
    app.add_plugin<AssetsPlugin>();
    auto& server = app.resource<AssetServer>();
    server.emplace_source<MemorySource>();
    server.add_loader<ServerAsset, DependentServerLoader>();
    server.add_loader<DependencyAsset, FailingDependencyLoader>();
    app.world().sort_systems();

    auto handle = app.resource<AssetServer>().load_async<ServerAsset>(
        AssetPath("memory://asset.bin")
    );
    auto& assets = app.resource<Assets<ServerAsset>>();

    run_post_update_until(app, [&]() {
        return assets.get(handle).has_value();
    });

    auto asset = assets.get(handle);
    REQUIRE(asset.has_value());
    auto dependency_id = asset->dependency.id();
    auto& dependencies = app.resource<Assets<DependencyAsset>>();

    run_post_update_until(app, [&]() {
        auto state = dependencies.load_state(dependency_id);
        return state && *state == AssetLoadState::Failed;
    });

    REQUIRE(app.resource<AssetServer>().is_loaded(handle));
    REQUIRE(
        app.resource<AssetServer>().dependency_load_state(handle) ==
        AssetLoadState::Failed
    );
    REQUIRE(
        app.resource<AssetServer>().recursive_dependency_load_state(handle) ==
        AssetLoadState::Failed
    );
    REQUIRE_FALSE(
        app.resource<AssetServer>().is_loaded_with_dependencies(handle)
    );

    auto load_error = app.resource<AssetServer>().load_error(
        AssetKey {
            .type = type_id<DependencyAsset>(),
            .id = dependency_id,
        }
    );
    REQUIRE(load_error.has_value());
    REQUIRE(load_error->path.as_string() == "memory://dependency.bin");
    REQUIRE(load_error->message == "dependency loader failed");

    auto failed_dependency =
        app.resource<AssetServer>().first_failed_dependency(handle);
    REQUIRE(failed_dependency.has_value());
    REQUIRE(failed_dependency->asset.type == type_id<DependencyAsset>());
    REQUIRE(failed_dependency->asset.id == dependency_id);
    REQUIRE(
        failed_dependency->error.path.as_string() == "memory://dependency.bin"
    );
    REQUIRE(failed_dependency->error.message == "dependency loader failed");
}

TEST_CASE(
    "AssetServer load_async reuses pending cached paths",
    "[asset][server][async]"
) {
    ServerLoader::load_count = 0;

    App app;
    app.add_plugin<TaskPlugin>();
    AssetServer server(&app);
    server.emplace_source<MemorySource>();
    app.add_resource(std::move(server));
    app.resource<AssetServer>().add_loader<ServerAsset, ServerLoader>();
    app.world().sort_systems();

    auto first = app.resource<AssetServer>().load_async<ServerAsset>(
        AssetPath("memory://asset.bin")
    );
    auto second = app.resource<AssetServer>().load_async<ServerAsset>(
        AssetPath("memory://asset.bin")
    );

    REQUIRE(first.id() == second.id());

    auto& assets = app.resource<Assets<ServerAsset>>();
    run_post_update_until(app, [&]() {
        return assets.get(first).has_value();
    });

    REQUIRE(ServerLoader::load_count.load() == 1);
}

TEST_CASE(
    "AssetServer load_async stores loader failures",
    "[asset][server][async]"
) {
    App app;
    app.add_plugin<TaskPlugin>();
    AssetServer server(&app);
    server.emplace_source<MemorySource>();
    app.add_resource(std::move(server));
    app.resource<AssetServer>().add_loader<ServerAsset, FailingServerLoader>();
    app.world().sort_systems();

    auto handle = app.resource<AssetServer>().load_async<ServerAsset>(
        AssetPath("memory://asset.bin")
    );
    auto& assets = app.resource<Assets<ServerAsset>>();

    run_post_update_until(app, [&]() {
        auto state = assets.load_state(handle);
        return state && *state == AssetLoadState::Failed;
    });

    REQUIRE_FALSE(assets.get(handle).has_value());
    auto error = assets.load_error(handle);
    REQUIRE(error.has_value());
    REQUIRE(error->path.as_string() == "memory://asset.bin");
    REQUIRE(error->message == "server loader failed");

    std::size_t last_event = 0;
    EventReader<AssetEvent<ServerAsset>> reader(
        app.resource<Events<AssetEvent<ServerAsset>>>(),
        last_event
    );
    bool saw_failed = false;
    while (auto event = reader.next()) {
        if (event->type == AssetEventType::Failed && event->id == handle.id()) {
            saw_failed = true;
        }
    }
    REQUIRE(saw_failed);
}

TEST_CASE(
    "AssetServer load_async discards completions for released handles",
    "[asset][server][async]"
) {
    ServerLoader::load_count = 0;

    App app;
    app.add_plugin<TaskPlugin>();
    AssetServer server(&app);
    server.emplace_source<MemorySource>();
    app.add_resource(std::move(server));
    app.resource<AssetServer>().add_loader<ServerAsset, ServerLoader>();
    app.world().sort_systems();

    AssetId id = 0;
    {
        auto handle = app.resource<AssetServer>().load_async<ServerAsset>(
            AssetPath("memory://asset.bin")
        );
        id = handle.id();
        auto state = app.resource<Assets<ServerAsset>>().load_state(id);
        REQUIRE(state.has_value());
        REQUIRE(*state == AssetLoadState::Loading);
    }

    auto& assets = app.resource<Assets<ServerAsset>>();
    app.run_schedule(PostUpdate);
    REQUIRE_FALSE(assets.load_state(id).has_value());

    run_post_update_until(app, [&]() {
        return ServerLoader::load_count.load() == 1;
    });
    REQUIRE_FALSE(assets.load_state(id).has_value());
}
