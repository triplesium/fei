#include "scene/scene.hpp"

#include "app/app.hpp"
#include "asset/assets.hpp"
#include "asset/plugin.hpp"
#include "asset/server.hpp"
#include "asset/source.hpp"
#include "core/image.hpp"
#include "core/transform.hpp"
#include "ecs/event.hpp"
#include "rendering/components.hpp"

#include <array>
#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>

using namespace fei;

namespace {

class MemorySource : public AssetSource {
  private:
    std::array<std::byte, 1> m_bytes {std::byte {1}};

  public:
    std::string name() const override { return "memory"; }

    bool exists(const std::filesystem::path& path) const override {
        auto asset_path = path.generic_string();
        return asset_path == "scene.obj" || asset_path == "albedo.png";
    }

    Result<Reader, std::string>
    try_get_reader(const std::filesystem::path& path) const override {
        if (!exists(path)) {
            return failure("memory asset not found: " + path.generic_string());
        }
        return Reader(m_bytes.data(), m_bytes.size());
    }
};

class BlockingImageLoader : public AssetLoader<Image> {
  public:
    static inline std::atomic<bool> allow_finish = true;

    AssetLoadResult<Image>
    load(Reader&, const LoadContext& /*context*/) override {
        while (!allow_finish.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds {1});
        }
        return Image::create_empty(
            1,
            1,
            1,
            PixelFormat::Rgba8Unorm,
            TextureUsage::Sampled,
            TextureType::Texture2D
        );
    }
};

class FailingImageLoader : public AssetLoader<Image> {
  public:
    AssetLoadResult<Image> load(Reader&, const LoadContext& context) override {
        return failure(AssetLoadError(context.asset_path(), "image failed"));
    }
};

class DependentSceneLoader : public AssetLoader<Scene> {
  public:
    AssetLoadResult<Scene> load(Reader&, const LoadContext& context) override {
        auto image = context.try_load<Image>(AssetPath("memory://albedo.png"));
        if (!image) {
            return failure(std::move(image).error());
        }

        auto scene = std::make_unique<Scene>();
        scene->meshes.push_back(
            std::make_unique<Mesh>(RenderPrimitive::Triangles)
        );

        auto material = std::make_unique<StandardMaterial>();
        material->albedo_map = std::move(*image);
        scene->materials.push_back(std::move(material));
        scene->objects.push_back(
            Scene::Object {
                .mesh_index = 0,
                .material_index = 0,
            }
        );
        return scene;
    }
};

struct ImageLoadGuard {
    ~ImageLoadGuard() { BlockingImageLoader::allow_finish = true; }
};

template<typename Done>
void run_post_update_until(App& app, Done done) {
    for (int i = 0; i < 1000 && !done(); ++i) {
        app.run_schedule(PostUpdate);
        std::this_thread::sleep_for(std::chrono::milliseconds {1});
    }
    REQUIRE(done());
}

std::size_t spawned_scene_object_count(App& app) {
    std::size_t count = 0;
    app.world().run_system_once(
        [&](Query<Entity, Mesh3d, MeshMaterial3d<StandardMaterial>, Transform3d>
                query) {
            count = query.size();
        }
    );
    return count;
}

} // namespace

TEST_CASE(
    "spawn_scene waits for recursive asset dependencies",
    "[scene][asset]"
) {
    BlockingImageLoader::allow_finish = false;
    ImageLoadGuard guard;

    App app;
    app.add_plugin<AssetsPlugin>();
    auto& asset_server = app.resource<AssetServer>();
    asset_server.emplace_source<MemorySource>();
    asset_server.add_loader<Scene, DependentSceneLoader>();
    asset_server.add_loader<Image, BlockingImageLoader>();
    asset_server.add_without_loader<Mesh>();
    asset_server.add_without_loader<StandardMaterial>();
    app.add_event<SceneSpawnedEvent>()
        .add_event<SceneSpawnFailedEvent>()
        .add_systems(Update, spawn_scene);
    app.world().sort_systems();

    auto scene_handle =
        asset_server.load_async<Scene>(AssetPath("memory://scene.obj"));
    auto spawner_entity = app.world().entity();
    app.world().add_component(
        spawner_entity,
        SceneSpawner {
            .scene = scene_handle,
            .options = {},
        }
    );

    run_post_update_until(app, [&]() {
        return app.resource<Assets<Scene>>().get(scene_handle).has_value();
    });

    REQUIRE_FALSE(asset_server.is_loaded_with_dependencies(scene_handle));

    app.run_schedule(Update);

    REQUIRE(app.world().has_entity(spawner_entity));
    REQUIRE(app.world().has_component<SceneSpawner>(spawner_entity));
    REQUIRE(spawned_scene_object_count(app) == 0);

    BlockingImageLoader::allow_finish = true;
    run_post_update_until(app, [&]() {
        return asset_server.is_loaded_with_dependencies(scene_handle);
    });

    app.run_schedule(Update);

    REQUIRE_FALSE(app.world().has_entity(spawner_entity));
    REQUIRE(spawned_scene_object_count(app) == 1);

    std::size_t last_spawned_event = 0;
    EventReader<SceneSpawnedEvent> spawned_reader(
        app.resource<Events<SceneSpawnedEvent>>(),
        last_spawned_event
    );
    auto spawned_event = spawned_reader.next();
    REQUIRE(spawned_event.has_value());
    REQUIRE(spawned_event->entity == spawner_entity);
    REQUIRE(spawned_event->scene.id() == scene_handle.id());
    REQUIRE_FALSE(spawned_reader.next().has_value());
}

TEST_CASE(
    "spawn_scene reports failed recursive asset dependencies",
    "[scene][asset]"
) {
    App app;
    app.add_plugin<AssetsPlugin>();
    auto& asset_server = app.resource<AssetServer>();
    asset_server.emplace_source<MemorySource>();
    asset_server.add_loader<Scene, DependentSceneLoader>();
    asset_server.add_loader<Image, FailingImageLoader>();
    asset_server.add_without_loader<Mesh>();
    asset_server.add_without_loader<StandardMaterial>();
    app.add_event<SceneSpawnedEvent>()
        .add_event<SceneSpawnFailedEvent>()
        .add_systems(Update, spawn_scene);
    app.world().sort_systems();

    auto scene_handle =
        asset_server.load_async<Scene>(AssetPath("memory://scene.obj"));
    auto spawner_entity = app.world().entity();
    app.world().add_component(
        spawner_entity,
        SceneSpawner {
            .scene = scene_handle,
            .options = {},
        }
    );

    run_post_update_until(app, [&]() {
        return app.resource<Assets<Scene>>().get(scene_handle).has_value();
    });

    auto scene = app.resource<Assets<Scene>>().get(scene_handle);
    REQUIRE(scene.has_value());
    REQUIRE(scene->materials.size() == 1);
    REQUIRE(scene->materials[0]->albedo_map.has_value());
    auto dependency_id = scene->materials[0]->albedo_map->id();

    run_post_update_until(app, [&]() {
        return asset_server.recursive_dependency_load_state(scene_handle) ==
               AssetLoadState::Failed;
    });

    app.run_schedule(Update);

    REQUIRE_FALSE(app.world().has_entity(spawner_entity));
    REQUIRE(spawned_scene_object_count(app) == 0);

    std::size_t last_event = 0;
    EventReader<SceneSpawnFailedEvent> reader(
        app.resource<Events<SceneSpawnFailedEvent>>(),
        last_event
    );
    auto event = reader.next();
    REQUIRE(event.has_value());
    REQUIRE(event->entity == spawner_entity);
    REQUIRE(event->scene.id() == scene_handle.id());
    REQUIRE(event->asset.type == type_id<Image>());
    REQUIRE(event->asset.id == dependency_id);
    REQUIRE(event->error.path.as_string() == "memory://albedo.png");
    REQUIRE(event->error.message == "image failed");
    REQUIRE_FALSE(reader.next().has_value());
}
