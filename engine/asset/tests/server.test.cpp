#include "asset/server.hpp"

#include "asset/assets.hpp"
#include "asset/io.hpp"
#include "asset/loader.hpp"
#include "asset/path.hpp"
#include "asset/plugin.hpp"
#include "asset/request.hpp"
#include "asset/serialization.hpp"
#include "asset/source.hpp"
#include "ecs/event.hpp"
#include "refl/cls.hpp"
#include "refl/ref.hpp"
#include "refl/registry.hpp"
#include "serialization/node.hpp"
#include "serialization/serializer.hpp"
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
#include <utility>

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

struct AssetHolder {
    Handle<ServerAsset> asset;
};

struct ProjectAssetHolder {
    Handle<ServerAsset> asset;
};

struct UnregisteredAsset {};

using ServerAssetLoadFn =
    Handle<ServerAsset> (AssetServer::*)(const AssetPath&);
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
    static inline std::atomic<std::size_t> asset_size {4};

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
        return Reader(m_bytes.data(), asset_size.load());
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

class ProjectMemorySource : public AssetSource {
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

class RawDependentServerLoader : public AssetLoader<ServerAsset> {
  public:
    AssetLoadResult<ServerAsset>
    load(Reader&, const LoadContext& context) override {
        auto dependency_path =
            context.asset_path().resolve_embed_str("dependency.bin");
        auto bytes = context.read_asset_bytes(dependency_path);
        if (!bytes) {
            return failure(bytes.error());
        }

        return std::make_unique<ServerAsset>(ServerAsset {
            .byte_count = static_cast<int>(bytes->size()),
            .path = dependency_path.as_string(),
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
    app.finish();

    REQUIRE(app.has_resource<AssetServer>());
    REQUIRE(app.has_resource<AssetLoadRequests>());
    REQUIRE(app.has_resource<Tasks>());
    REQUIRE(app.has_plugin<TaskPlugin>());
    CHECK(app.resource<AssetServer>().default_source() == "project");
    CHECK(app.resource<AssetServer>().has_source("project"));
    CHECK(app.resource<AssetServer>().has_source("embedded"));
    CHECK(
        app.resource<AssetServer>().canonicalize_path(
            AssetPath("textures/player.png")
        ) == AssetPath("project://textures/player.png")
    );
}

TEST_CASE(
    "AssetsPlugin rebinds AssetServer after App relocation",
    "[asset][plugin][app]"
) {
    App app;
    app.add_plugin<AssetsPlugin>();
    app.finish();
    app.resource<AssetServer>().emplace_source<MemorySource>();
    app.resource<AssetServer>().add_loader<ServerAsset, ServerLoader>();

    App relocated(std::move(app));
    auto handle = relocated.resource<AssetServer>().load<ServerAsset>(
        AssetPath("memory://asset.bin")
    );
    auto asset = relocated.resource<Assets<ServerAsset>>().get(handle);

    REQUIRE(asset.has_value());
    CHECK(asset->byte_count == 4);
    CHECK(relocated.resource<AssetServer>().is_loaded(handle));
    auto loaded_path = relocated.resource<AssetServer>().asset_path(
        AssetServer::asset_key(handle)
    );
    REQUIRE(loaded_path);
    CHECK(*loaded_path == AssetPath("memory://asset.bin"));
    CHECK_FALSE(relocated.resource<AssetServer>().load_error(handle));
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
    "AssetServer loads registered asset types through type-erased access",
    "[asset][server][untyped]"
) {
    App app;
    AssetServer server(&app);
    server.emplace_source<MemorySource>();
    app.add_resource(std::move(server));
    auto& asset_server = app.resource<AssetServer>();
    asset_server.add_loader<ServerAsset, ServerLoader>();

    REQUIRE(asset_server.has_asset_type(type_id<ServerAsset>()));
    REQUIRE_FALSE(asset_server.has_asset_type(type_id<UnregisteredAsset>()));

    auto loaded = asset_server.load(
        type_id<ServerAsset>(),
        AssetPath("memory://asset.bin")
    );
    REQUIRE(loaded);
    auto handle = std::move(*loaded);
    CHECK(handle.asset_type() == type_id<ServerAsset>());
    CHECK(handle.is<ServerAsset>());
    CHECK_FALSE(handle.is<DependencyAsset>());
    CHECK(handle.id() != invalid_asset_id);
    CHECK(asset_server.is_loaded(handle));

    auto typed = handle.try_typed<ServerAsset>();
    REQUIRE(typed);
    REQUIRE_FALSE(handle.try_typed<DependencyAsset>());
    auto asset = app.resource<Assets<ServerAsset>>().get(*typed);
    REQUIRE(asset);
    CHECK(asset->byte_count == 4);

    auto value = asset_server.handle_value(handle);
    REQUIRE(value);
    CHECK(value->type_id() == type_id<Handle<ServerAsset>>());
    auto key = asset_server.asset_key(value->ref());
    REQUIRE(key);
    CHECK(key->type == type_id<ServerAsset>());
    CHECK(key->id == handle.id());
    auto non_handle = make_val<int>(7);
    auto invalid_key = asset_server.asset_key(non_handle.ref());
    REQUIRE_FALSE(invalid_key);
    CHECK(
        invalid_key.error().message.contains("not a registered asset handle")
    );

    auto missing = asset_server.load(
        type_id<UnregisteredAsset>(),
        AssetPath("memory://asset.bin")
    );
    REQUIRE_FALSE(missing);
    CHECK(missing.error().type == type_id<UnregisteredAsset>());
    CHECK(missing.error().message.contains("No asset type registered"));

    auto& assets = app.resource<Assets<ServerAsset>>();
    typed = nullopt;
    value = Val {};
    CHECK(assets.unload_unused() == 0);
    handle = UntypedHandle {};
    CHECK(assets.unload_unused() == 1);
}

TEST_CASE(
    "AssetServer reload preserves handles and replaces contents",
    "[asset][server][reload]"
) {
    MemorySource::asset_size = 4;
    App app;
    AssetServer server(&app);
    server.emplace_source<MemorySource>();
    app.add_resource(std::move(server));
    app.resource<AssetServer>().add_loader<ServerAsset, ServerLoader>();

    auto handle = app.resource<AssetServer>().load<ServerAsset>(
        AssetPath("memory://asset.bin")
    );
    REQUIRE(app.resource<Assets<ServerAsset>>().get(handle)->byte_count == 4);

    MemorySource::asset_size = 3;
    auto reloaded = app.resource<AssetServer>().reload<ServerAsset>(
        AssetPath("memory://asset.bin")
    );

    REQUIRE(reloaded);
    CHECK(reloaded->id() == handle.id());
    REQUIRE(app.resource<Assets<ServerAsset>>().get(handle));
    CHECK(app.resource<Assets<ServerAsset>>().get(handle)->byte_count == 3);
    MemorySource::asset_size = 4;
}

TEST_CASE(
    "AssetServer reloads a path only when it is already loaded",
    "[asset][server][reload]"
) {
    MemorySource::asset_size = 4;
    App app;
    AssetServer server(&app);
    server.emplace_source<MemorySource>();
    app.add_resource(std::move(server));
    app.resource<AssetServer>().add_loader<ServerAsset, ServerLoader>();

    auto not_loaded = app.resource<AssetServer>().reload_if_loaded<ServerAsset>(
        AssetPath("memory://dependency.bin")
    );
    REQUIRE(not_loaded);
    CHECK_FALSE(*not_loaded);

    auto handle = app.resource<AssetServer>().load<ServerAsset>(
        AssetPath("memory://asset.bin")
    );
    MemorySource::asset_size = 2;
    auto reloaded = app.resource<AssetServer>().reload_if_loaded<ServerAsset>(
        AssetPath("memory://asset.bin")
    );
    REQUIRE(reloaded);
    CHECK(*reloaded);
    CHECK(app.resource<Assets<ServerAsset>>().get(handle)->byte_count == 2);
    MemorySource::asset_size = 4;
}

TEST_CASE(
    "AssetServer remaps loaded paths across registered asset types",
    "[asset][server][path]"
) {
    App app;
    AssetServer server(&app);
    server.emplace_source<MemorySource>();
    app.add_resource(std::move(server));
    app.resource<AssetServer>().add_loader<ServerAsset, ServerLoader>();
    const AssetPath source("memory://asset.bin");
    const AssetPath destination("memory://renamed.bin");
    auto handle = app.resource<AssetServer>().load<ServerAsset>(source);

    CHECK(app.resource<AssetServer>().remap_path(source, destination) == 1);
    auto& assets = app.resource<Assets<ServerAsset>>();
    REQUIRE(assets.path(handle));
    CHECK(*assets.path(handle) == destination);
    auto reloaded = app.resource<AssetServer>().load<ServerAsset>(destination);
    CHECK(reloaded.id() == handle.id());
}

TEST_CASE(
    "AssetServer removes loaded assets by path across registered types",
    "[asset][server][path]"
) {
    App app;
    AssetServer server(&app);
    server.emplace_source<MemorySource>();
    app.add_resource(std::move(server));
    app.resource<AssetServer>().add_loader<ServerAsset, ServerLoader>();
    const AssetPath path("memory://asset.bin");
    auto handle = app.resource<AssetServer>().load<ServerAsset>(path);

    CHECK(app.resource<AssetServer>().remove_path(path) == 1);
    CHECK_FALSE(app.resource<Assets<ServerAsset>>().get(handle));
}

TEST_CASE(
    "AssetServer canonicalizes default source paths before caching",
    "[asset][server][path]"
) {
    App app;
    AssetServer server(&app);
    server.emplace_source<MemorySource>();
    REQUIRE(server.set_default_source("memory"));
    app.add_resource(std::move(server));
    app.resource<AssetServer>().add_loader<ServerAsset, ServerLoader>();

    auto first =
        app.resource<AssetServer>().load<ServerAsset>(AssetPath("./asset.bin"));
    auto second = app.resource<AssetServer>().load<ServerAsset>(
        AssetPath("memory://asset.bin")
    );

    CHECK(first.id() == second.id());
    auto& assets = app.resource<Assets<ServerAsset>>();
    auto stored_path = assets.path(first);
    REQUIRE(stored_path);
    CHECK(*stored_path == AssetPath("memory://asset.bin"));
    auto asset = assets.get(first);
    REQUIRE(asset);
    CHECK(asset->path == "memory://asset.bin");

    auto bytes =
        app.resource<AssetServer>().read_asset_bytes(AssetPath("asset.bin"));
    REQUIRE(bytes);
    CHECK(bytes->size() == 4);
}

TEST_CASE(
    "Asset handle codecs persist source paths and resolve through the server",
    "[asset][serialization]"
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
    auto stored_path = assets.path(handle);
    REQUIRE(stored_path);
    CHECK(stored_path->as_string() == "memory://asset.bin");

    Registry::instance().register_cls<AssetHolder>().add_property(
        "asset",
        &AssetHolder::asset
    );
    serialization::ValueCodecRegistry codecs;
    REQUIRE(
        register_asset_handle_codec<ServerAsset>(
            codecs,
            app.resource<AssetServer>(),
            assets
        )
    );

    const AssetHolder expected {.asset = handle};
    auto node = serialization::serialize(
        Ref(expected),
        serialization::SerializeOptions {
            .include_type_tag = false,
            .codecs = &codecs,
        }
    );
    REQUIRE(node);
    const auto* object = node->try_object();
    REQUIRE(object);
    const auto* asset = serialization::find_field(*object, "asset");
    REQUIRE(asset);
    const auto* asset_object = asset->value.try_object();
    REQUIRE(asset_object);
    const auto* encoded = serialization::find_field(*asset_object, "$asset");
    REQUIRE(encoded);
    const auto* reference = encoded->value.try_object();
    REQUIRE(reference);
    CHECK_FALSE(serialization::find_field(*reference, "id"));
    const auto* encoded_path = serialization::find_field(*reference, "path");
    REQUIRE(encoded_path);
    REQUIRE(encoded_path->value.try_string());
    CHECK(*encoded_path->value.try_string() == "memory://asset.bin");

    auto decoded = serialization::deserialize(
        type_id<AssetHolder>(),
        *node,
        serialization::DeserializeOptions {.codecs = &codecs}
    );
    REQUIRE(decoded);
    CHECK(decoded->get<AssetHolder>().asset.id() == handle.id());

    auto legacy = serialization::SerializedNode::object({
        serialization::SerializedField {
            .name = "asset",
            .value = serialization::SerializedNode::object({
                serialization::SerializedField {
                    .name = "$asset",
                    .value = serialization::SerializedNode::string(
                        "memory://asset.bin"
                    ),
                },
            }),
        },
    });
    auto legacy_decoded = serialization::deserialize(
        type_id<AssetHolder>(),
        legacy,
        serialization::DeserializeOptions {.codecs = &codecs}
    );
    REQUIRE(legacy_decoded);
    CHECK(legacy_decoded->get<AssetHolder>().asset.id() == handle.id());

    const AssetHolder empty;
    auto empty_node = serialization::serialize(
        Ref(empty),
        serialization::SerializeOptions {
            .include_type_tag = false,
            .codecs = &codecs,
        }
    );
    REQUIRE(empty_node);
    auto empty_decoded = serialization::deserialize(
        type_id<AssetHolder>(),
        *empty_node,
        serialization::DeserializeOptions {.codecs = &codecs}
    );
    REQUIRE(empty_decoded);
    CHECK_FALSE(empty_decoded->get<AssetHolder>().asset);
}

TEST_CASE(
    "Project asset handle codecs resolve moved assets by UUID",
    "[asset][serialization][uuid]"
) {
    const auto id = AssetUuid::random();
    const AssetPath original_path("project://old.bin");
    const AssetPath moved_path("project://moved/new.bin");

    App save_app;
    save_app.add_resource(AssetDatabase(std::filesystem::current_path()));
    REQUIRE(save_app.resource<AssetDatabase>().register_metadata(
        original_path,
        AssetMetadata {
            .id = id,
            .importer = "test",
            .settings = {},
        }
    ));
    AssetServer save_server(&save_app);
    save_server.emplace_source<ProjectMemorySource>();
    save_app.add_resource(std::move(save_server));
    save_app.resource<AssetServer>().add_loader<ServerAsset, ServerLoader>();
    auto saved_handle =
        save_app.resource<AssetServer>().load<ServerAsset>(original_path);

    Registry::instance().register_cls<ProjectAssetHolder>().add_property(
        "asset",
        &ProjectAssetHolder::asset
    );
    serialization::ValueCodecRegistry save_codecs;
    REQUIRE(
        register_asset_handle_codec<ServerAsset>(
            save_codecs,
            save_app.resource<AssetServer>(),
            save_app.resource<Assets<ServerAsset>>()
        )
    );
    const ProjectAssetHolder saved {.asset = saved_handle};
    auto node = serialization::serialize(
        Ref(saved),
        serialization::SerializeOptions {
            .include_type_tag = false,
            .codecs = &save_codecs,
        }
    );
    REQUIRE(node);

    const auto* holder_object = node->try_object();
    REQUIRE(holder_object);
    const auto* asset = serialization::find_field(*holder_object, "asset");
    REQUIRE(asset);
    const auto* asset_object = asset->value.try_object();
    REQUIRE(asset_object);
    const auto* encoded = serialization::find_field(*asset_object, "$asset");
    REQUIRE(encoded);
    const auto* reference = encoded->value.try_object();
    REQUIRE(reference);
    const auto* encoded_id = serialization::find_field(*reference, "id");
    REQUIRE(encoded_id);
    REQUIRE(encoded_id->value.try_string());
    CHECK(*encoded_id->value.try_string() == id.as_string());

    App load_app;
    load_app.add_resource(AssetDatabase(std::filesystem::current_path()));
    REQUIRE(load_app.resource<AssetDatabase>().register_metadata(
        moved_path,
        AssetMetadata {
            .id = id,
            .importer = "test",
            .settings = {},
        }
    ));
    AssetServer load_server(&load_app);
    load_server.emplace_source<ProjectMemorySource>();
    load_app.add_resource(std::move(load_server));
    load_app.resource<AssetServer>().add_loader<ServerAsset, ServerLoader>();
    serialization::ValueCodecRegistry load_codecs;
    REQUIRE(
        register_asset_handle_codec<ServerAsset>(
            load_codecs,
            load_app.resource<AssetServer>(),
            load_app.resource<Assets<ServerAsset>>()
        )
    );

    auto loaded_by_id = load_app.resource<AssetServer>().load<ServerAsset>(id);
    const auto by_id =
        load_app.resource<Assets<ServerAsset>>().get(loaded_by_id);
    REQUIRE(by_id);
    CHECK(by_id->path == moved_path.as_string());

    auto decoded = serialization::deserialize(
        type_id<ProjectAssetHolder>(),
        *node,
        serialization::DeserializeOptions {.codecs = &load_codecs}
    );

    REQUIRE(decoded);
    const auto loaded = load_app.resource<Assets<ServerAsset>>().get(
        decoded->get<ProjectAssetHolder>().asset
    );
    REQUIRE(loaded);
    CHECK(loaded->path == moved_path.as_string());
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

TEST_CASE(
    "AssetServer reads and records sync raw asset dependencies",
    "[asset][server]"
) {
    App app;
    AssetServer server(&app);
    server.emplace_source<MemorySource>();
    app.add_resource(std::move(server));
    app.resource<AssetServer>()
        .add_loader<ServerAsset, RawDependentServerLoader>();

    auto handle = app.resource<AssetServer>().load<ServerAsset>(
        AssetPath("memory://asset.bin")
    );
    auto& assets = app.resource<Assets<ServerAsset>>();
    auto asset = assets.get(handle);

    REQUIRE(asset.has_value());
    REQUIRE(asset->byte_count == 2);
    REQUIRE(asset->path == "memory://dependency.bin");
    auto dependencies = assets.loader_dependencies(handle);
    REQUIRE(dependencies.has_value());
    REQUIRE(dependencies->size() == 1);
    REQUIRE((*dependencies)[0] == AssetPath("memory://dependency.bin"));
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
    app.finish();
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
    app.finish();
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
    "AssetServer loads registered asset types asynchronously through "
    "type-erased access",
    "[asset][server][async][untyped]"
) {
    ServerLoader::load_count = 0;

    App app;
    app.add_plugin<TaskPlugin>();
    app.finish();
    AssetServer server(&app);
    server.emplace_source<MemorySource>();
    app.add_resource(std::move(server));
    auto& asset_server = app.resource<AssetServer>();
    asset_server.add_loader<ServerAsset, ServerLoader>();
    app.world().sort_systems();

    auto loaded = asset_server.load_async(
        type_id<ServerAsset>(),
        AssetPath("memory://asset.bin")
    );
    REQUIRE(loaded);
    auto handle = std::move(*loaded);
    REQUIRE(handle.is<ServerAsset>());
    auto state = asset_server.load_state(handle);
    REQUIRE(state);
    REQUIRE(*state == AssetLoadState::Loading);

    for (int i = 0; i < 1000 && ServerLoader::load_count.load() != 1; ++i) {
        app.resource<Tasks>().drain_completions();
        std::this_thread::sleep_for(std::chrono::milliseconds {1});
    }
    REQUIRE(ServerLoader::load_count.load() == 1);

    run_post_update_until(app, [&]() {
        return asset_server.is_loaded(handle);
    });

    auto typed = handle.try_typed<ServerAsset>();
    REQUIRE(typed);
    auto asset = app.resource<Assets<ServerAsset>>().get(*typed);
    REQUIRE(asset);
    CHECK(asset->byte_count == 4);
    CHECK(asset->path == "memory://asset.bin");
}

TEST_CASE(
    "AssetServer load_async lets loaders request dependencies",
    "[asset][server][async]"
) {
    DependentServerLoader::load_count = 0;
    DependencyLoader::load_count = 0;

    App app;
    app.add_plugin<AssetsPlugin>();
    app.finish();
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
    "AssetServer reads and records async raw asset dependencies",
    "[asset][server][async]"
) {
    App app;
    app.add_plugin<AssetsPlugin>();
    app.finish();
    auto& server = app.resource<AssetServer>();
    server.emplace_source<MemorySource>();
    server.add_loader<ServerAsset, RawDependentServerLoader>();
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
    REQUIRE(asset->byte_count == 2);
    REQUIRE(asset->path == "memory://dependency.bin");
    auto dependencies = assets.loader_dependencies(handle);
    REQUIRE(dependencies.has_value());
    REQUIRE(dependencies->size() == 1);
    REQUIRE((*dependencies)[0] == AssetPath("memory://dependency.bin"));
}

TEST_CASE(
    "AssetServer reports async dependency load failures",
    "[asset][server][async]"
) {
    App app;
    app.add_plugin<AssetsPlugin>();
    app.finish();
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
    app.finish();
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
    app.finish();
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
    app.finish();
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
