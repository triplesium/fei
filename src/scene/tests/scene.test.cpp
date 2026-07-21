#include "scene/scene.hpp"

#include "app/app.hpp"
#include "asset/assets.hpp"
#include "asset/plugin.hpp"
#include "asset/server.hpp"
#include "asset/source.hpp"
#include "core/image.hpp"
#include "core/transform.hpp"
#include "ecs/event.hpp"
#include "ecs/hierarchy.hpp"
#include "rendering/components.hpp"
#include "scene/plugin.hpp"

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
        auto image = context.load<Image>(AssetPath("memory://albedo.png"));

        auto scene = std::make_unique<Scene>();
        auto mesh = context.add_asset<Mesh>(
            std::make_unique<Mesh>(RenderPrimitive::Triangles)
        );

        auto material = std::make_unique<StandardMaterial>();
        material->albedo_map = std::move(image);
        auto material_handle =
            context.add_asset<StandardMaterial>(std::move(material));
        auto scene_mesh = std::make_unique<SceneMesh>();
        scene_mesh->primitives.push_back(
            ScenePrimitive {
                .mesh = mesh,
                .material = material_handle,
            }
        );
        auto scene_mesh_handle =
            context.add_asset<SceneMesh>(std::move(scene_mesh));
        scene->nodes.push_back(SceneNode {.mesh = scene_mesh_handle});
        scene->roots.push_back(0);
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

struct HierarchicalSceneAssets {
    Handle<Scene> scene;
    Handle<SceneMesh> scene_mesh;
    Handle<Mesh> mesh;
    Handle<StandardMaterial> material;
};

HierarchicalSceneAssets add_hierarchical_scene(App& app) {
    auto mesh =
        app.resource<Assets<Mesh>>().emplace(RenderPrimitive::Triangles);
    auto material = app.resource<Assets<StandardMaterial>>().emplace();

    auto scene_mesh_asset = std::make_unique<SceneMesh>();
    scene_mesh_asset->name = "shared-mesh";
    scene_mesh_asset->primitives = {
        ScenePrimitive {.mesh = mesh, .material = material},
        ScenePrimitive {.mesh = mesh, .material = material},
    };
    auto scene_mesh =
        app.resource<Assets<SceneMesh>>().add(std::move(scene_mesh_asset));

    auto scene_asset = std::make_unique<Scene>();
    scene_asset->nodes = {
        SceneNode {.name = "root-a", .children = {1}},
        SceneNode {
            .name = "mesh-parent",
            .local_transform = Transform3d {.position = {1.0f, 2.0f, 3.0f}},
            .mesh = scene_mesh,
            .children = {2},
        },
        SceneNode {.name = "mesh-child", .mesh = scene_mesh},
        SceneNode {.name = "root-b"},
    };
    scene_asset->roots = {0, 3};
    auto scene = app.resource<Assets<Scene>>().add(std::move(scene_asset));

    return HierarchicalSceneAssets {
        .scene = scene,
        .scene_mesh = scene_mesh,
        .mesh = mesh,
        .material = material,
    };
}

void setup_hierarchical_scene_app(App& app) {
    app.add_plugin<AssetsPlugin>().add_plugin<ScenePlugin>();
    auto& asset_server = app.resource<AssetServer>();
    asset_server.add_without_loader<Mesh>();
    asset_server.add_without_loader<StandardMaterial>();
    app.world().sort_systems();
}

} // namespace

TEST_CASE("scene assets describe valid node hierarchies", "[scene][asset]") {
    Scene scene;
    scene.nodes = {
        SceneNode {.name = "root-a", .children = {1, 2}},
        SceneNode {.name = "child"},
        SceneNode {.name = "branch", .children = {3}},
        SceneNode {.name = "leaf"},
        SceneNode {.name = "root-b"},
    };
    scene.roots = {0, 4};

    REQUIRE(scene.validate());
    REQUIRE(scene.node(3));
    CHECK(scene.node(3)->name == "leaf");
    CHECK_FALSE(scene.node(5));
}

TEST_CASE("scene hierarchy validation rejects invalid graphs", "[scene]") {
    Scene scene;

    SECTION("root index is out of range") {
        scene.nodes.resize(1);
        scene.roots = {1};

        auto status = scene.validate();
        REQUIRE_FALSE(status);
        CHECK(status.error().kind == SceneValidationError::Kind::InvalidRoot);
        CHECK(status.error().node == 1);
    }

    SECTION("child index is out of range") {
        scene.nodes = {SceneNode {.children = {1}}};
        scene.roots = {0};

        auto status = scene.validate();
        REQUIRE_FALSE(status);
        CHECK(status.error().kind == SceneValidationError::Kind::InvalidChild);
        CHECK(status.error().node == 0);
        REQUIRE(status.error().related_node);
        CHECK(*status.error().related_node == 1);
    }

    SECTION("node has multiple parents") {
        scene.nodes = {
            SceneNode {.children = {2}},
            SceneNode {.children = {2}},
            SceneNode {},
        };
        scene.roots = {0, 1};

        auto status = scene.validate();
        REQUIRE_FALSE(status);
        CHECK(
            status.error().kind == SceneValidationError::Kind::MultipleParents
        );
        CHECK(status.error().node == 2);
    }

    SECTION("hierarchy contains a cycle") {
        scene.nodes = {
            SceneNode {.children = {1}},
            SceneNode {.children = {0}},
        };

        auto status = scene.validate();
        REQUIRE_FALSE(status);
        CHECK(status.error().kind == SceneValidationError::Kind::Cycle);
    }

    SECTION("node is unreachable from roots") {
        scene.nodes.resize(2);
        scene.roots = {0};

        auto status = scene.validate();
        REQUIRE_FALSE(status);
        CHECK(
            status.error().kind == SceneValidationError::Kind::UnreachableNode
        );
        CHECK(status.error().node == 1);
    }
}

TEST_CASE("ScenePlugin registers SceneMesh assets", "[scene][plugin]") {
    App app;
    app.add_plugin<AssetsPlugin>().add_plugin<ScenePlugin>();

    CHECK(app.has_resource<Assets<SceneMesh>>());
}

TEST_CASE("SceneLoader maps OBJ shapes to scene nodes", "[scene][loader]") {
    App app;
    app.add_plugin<AssetsPlugin>().add_plugin<ScenePlugin>();
    auto& asset_server = app.resource<AssetServer>();
    asset_server.add_without_loader<Mesh>();
    asset_server.add_without_loader<StandardMaterial>();

    auto scene_handle = asset_server.load<Scene>("quad.obj");
    auto scene = app.resource<Assets<Scene>>().get(scene_handle);
    REQUIRE(scene);
    REQUIRE(scene->validate());
    REQUIRE(scene->nodes.size() == 1);
    REQUIRE(scene->roots == std::vector<SceneNodeId> {0});
    REQUIRE(scene->nodes[0].mesh);

    auto scene_mesh =
        app.resource<Assets<SceneMesh>>().get(*scene->nodes[0].mesh);
    REQUIRE(scene_mesh);
    REQUIRE(scene_mesh->validate());
    REQUIRE(scene_mesh->primitives.size() == 1);
    CHECK(scene_mesh->primitives[0].mesh);
    CHECK(scene_mesh->primitives[0].material);
}

TEST_CASE(
    "SceneSpawner instantiates hierarchical scene assets",
    "[scene][spawn]"
) {
    App app;
    setup_hierarchical_scene_app(app);
    auto assets = add_hierarchical_scene(app);

    auto first_owner = app.world().entity();
    app.world().add_component(
        first_owner,
        SceneSpawner {
            .scene = assets.scene,
            .options = SceneSpawnOptions {.scale = {2.0f, 3.0f, 4.0f}},
        }
    );
    app.run_schedule(Update);

    REQUIRE(app.world().has_component<SceneInstance>(first_owner));
    const auto first_instance =
        app.world().get_component<SceneInstance>(first_owner);
    REQUIRE(first_instance.node_entities.size() == 4);
    REQUIRE(app.world().has_entity(first_instance.root));
    REQUIRE(app.world().parent(first_instance.root));
    CHECK(*app.world().parent(first_instance.root) == first_owner);
    CHECK(
        (app.world().get_component<Transform3d>(first_instance.root).scale ==
         Vector3 {2.0f, 3.0f, 4.0f})
    );

    const auto root_a = *first_instance.node_entity(0);
    const auto mesh_parent = *first_instance.node_entity(1);
    const auto mesh_child = *first_instance.node_entity(2);
    const auto root_b = *first_instance.node_entity(3);
    CHECK(*app.world().parent(root_a) == first_instance.root);
    CHECK(*app.world().parent(mesh_parent) == root_a);
    CHECK(*app.world().parent(mesh_child) == mesh_parent);
    CHECK(*app.world().parent(root_b) == first_instance.root);
    CHECK(
        app.world().get_component<SceneNodeName>(mesh_parent).value ==
        "mesh-parent"
    );
    CHECK(app.world().get_component<SceneNodeInstance>(mesh_child).node == 2);

    std::size_t primitive_count = 0;
    app.world().run_system_once(
        [&](Query<Entity, const Mesh3d, const ChildOf> primitives) {
            for (const auto& [entity, mesh, child_of] : primitives) {
                (void)entity;
                CHECK(mesh.mesh.id() == assets.mesh.id());
                CHECK(
                    (child_of.parent == mesh_parent ||
                     child_of.parent == mesh_child)
                );
                ++primitive_count;
            }
        }
    );
    CHECK(primitive_count == 4);

    auto second_owner = app.world().entity();
    app.world().add_component(
        second_owner,
        SceneSpawner {.scene = assets.scene, .options = {}}
    );
    app.run_schedule(Update);

    const auto& second_instance =
        app.world().get_component<SceneInstance>(second_owner);
    CHECK(second_instance.root != first_instance.root);
    REQUIRE(second_instance.node_entities.size() == 4);
    for (std::size_t index = 0; index < first_instance.node_entities.size();
         ++index) {
        CHECK(
            second_instance.node_entities[index] !=
            first_instance.node_entities[index]
        );
    }
    CHECK(spawned_scene_object_count(app) == 8);
}

TEST_CASE(
    "SceneInstance owns replacement and cleanup lifecycles",
    "[scene][spawn][hierarchy]"
) {
    App app;
    setup_hierarchical_scene_app(app);
    auto assets = add_hierarchical_scene(app);

    auto owner = app.world().entity();
    app.world().add_component(
        owner,
        SceneSpawner {.scene = assets.scene, .options = {}}
    );
    app.run_schedule(Update);

    auto first_root = app.world().get_component<SceneInstance>(owner).root;
    app.world().add_component(
        owner,
        SceneSpawner {.scene = assets.scene, .options = {}}
    );
    app.run_schedule(Update);

    const auto& replacement = app.world().get_component<SceneInstance>(owner);
    CHECK_FALSE(app.world().has_entity(first_root));
    CHECK(app.world().has_entity(replacement.root));
    const auto replacement_root = replacement.root;
    const auto replacement_nodes = replacement.node_entities;

    app.world().remove_component<SceneInstance>(owner);
    app.run_schedule(Update);

    CHECK(app.world().has_entity(owner));
    CHECK_FALSE(app.world().has_entity(replacement_root));
    for (auto node : replacement_nodes) {
        CHECK_FALSE(app.world().has_entity(node));
    }

    app.world().add_component(
        owner,
        SceneSpawner {.scene = assets.scene, .options = {}}
    );
    app.run_schedule(Update);
    const auto final_instance = app.world().get_component<SceneInstance>(owner);
    app.world().despawn(owner);

    CHECK_FALSE(app.world().has_entity(final_instance.root));
    for (auto node : final_instance.node_entities) {
        CHECK_FALSE(app.world().has_entity(node));
    }
}

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
    asset_server.add_without_loader<SceneMesh>();
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

    REQUIRE(app.world().has_entity(spawner_entity));
    REQUIRE_FALSE(app.world().has_component<SceneSpawner>(spawner_entity));
    REQUIRE(app.world().has_component<SceneInstance>(spawner_entity));
    REQUIRE(spawned_scene_object_count(app) == 1);

    const auto& scene_instance =
        app.world().get_component<SceneInstance>(spawner_entity);
    REQUIRE(scene_instance.scene.id() == scene_handle.id());
    REQUIRE(scene_instance.node_entities.size() == 1);
    REQUIRE(app.world().has_entity(scene_instance.root));
    CHECK(*app.world().parent(scene_instance.root) == spawner_entity);
    CHECK(
        *app.world().parent(scene_instance.node_entities[0]) ==
        scene_instance.root
    );

    std::size_t last_spawned_event = 0;
    EventReader<SceneSpawnedEvent> spawned_reader(
        app.resource<Events<SceneSpawnedEvent>>(),
        last_spawned_event
    );
    auto spawned_event = spawned_reader.next();
    REQUIRE(spawned_event.has_value());
    REQUIRE(spawned_event->entity == spawner_entity);
    REQUIRE(spawned_event->scene.id() == scene_handle.id());
    REQUIRE(spawned_event->instance_root == scene_instance.root);
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
    asset_server.add_without_loader<SceneMesh>();
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
    REQUIRE(scene->nodes.size() == 1);
    REQUIRE(scene->nodes[0].mesh);
    auto scene_mesh =
        app.resource<Assets<SceneMesh>>().get(*scene->nodes[0].mesh);
    REQUIRE(scene_mesh);
    REQUIRE(scene_mesh->primitives.size() == 1);
    auto material = app.resource<Assets<StandardMaterial>>().get(
        scene_mesh->primitives[0].material
    );
    REQUIRE(material.has_value());
    REQUIRE(material->albedo_map.has_value());
    auto dependency_id = material->albedo_map->id();

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
