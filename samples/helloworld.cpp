#include "app/asset.hpp"
#include "ecs/commands.hpp"
#include "ecs/query.hpp"
#include "ecs/system.hpp"
#include "ecs/system_params.hpp"
#include "ecs/world.hpp"
#include "refl/registry.hpp"

#include <fstream>
#include <print>
#include <string>

using namespace fei;

struct Transform {
    float x, y;
};

struct Config {
    std::string name;
};

class TextLoader : public AssetLoader {
  public:
    Val load(const std::filesystem::path path) override {
        std::ifstream file(path);
        if (!file.is_open()) {
            error("Failed to open file: {}", path.string());
            return {};
        }
        std::string content(
            (std::istreambuf_iterator<char>(file)),
            std::istreambuf_iterator<char>()
        );
        return make_val<std::string>(std::move(content));
    }
};

int main() {
    // Register the Transform type with the reflection system
    Registry::instance().register_type<Transform>();

    World world;
    world.add_resource(CommandsQueue {});

    auto entity1 = world.entity();
    world.add_component(entity1, Transform {1.0f, 2.0f});
    auto entity2 = world.entity();
    world.add_component(entity2, Transform {3.0f, 4.0f});

    world.add_resource(Config {.name = "HelloConfig"});
    world.add_resource(AssetServer {});
    std::println("{}", world.get_resource<AssetServer>().assets_dir().string());

    FunctionSystem system0([](Commands commands) {
        auto entity3 = commands.spawn().add(Transform {5.0f, 6.0f}).id();
        std::println("System0");
        std::println("Spawned entity {}", entity3);
    });
    system0.run(world);
    world.get_resource<CommandsQueue>().execute(world);

    FunctionSystem system1([](Query<Entity, Transform> query,
                              Res<Config> config) {
        std::println("System1");
        std::println("{}", config->name);
        config->name = "UpdatedConfig";
        for (auto [entity, transform] : query) {
            std::println(
                "Entity {} Transform at ({}, {})",
                entity,
                transform.x,
                transform.y
            );
            transform.x += 1.0f;
            transform.y += 1.0f;
        }
    });
    system1.run(world);

    FunctionSystem system2([](Query<Entity, Transform> query,
                              Res<Config> config) {
        std::println("System2");
        std::println("{}", config->name);
        for (auto [entity, transform] : query) {
            std::println(
                "Entity {} Transform at ({}, {})",
                entity,
                transform.x,
                transform.y
            );
        }
    });
    system2.run(world);

    world.get_resource<AssetServer>().add_loader<std::string, TextLoader>();
    world.run_system_once([](Res<AssetServer> asset_server) {
        auto handle = asset_server->load<std::string>("test.txt");
        std::println("{}", *handle.get());
    });

    return 0;
}
