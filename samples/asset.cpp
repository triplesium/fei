#include "app/app.hpp"
#include "asset/event.hpp"
#include "asset/handle.hpp"
#include "asset/plugin.hpp"
#include "asset/server.hpp"
#include "core/text.hpp"
#include "ecs/commands.hpp"
#include "ecs/event.hpp"
#include "ecs/query.hpp"
#include "ecs/system_params.hpp"

#include <chrono>
#include <print>
#include <thread>

using namespace fei;

struct Foo {
    Handle<TextAsset> handle;
};

void startup(
    ResRW<AssetServer> asset_server,
    Commands commands,
    ResRW<Assets<TextAsset>> assets
) {
    commands.spawn().add(Foo {asset_server->load<TextAsset>("test.txt")});
    commands.spawn().add(
        Foo {assets->add(std::make_unique<TextAsset>("Hello World2"))}
    );
}

void update(Query<Foo> query, ResRW<Assets<TextAsset>> assets) {
    for (auto [foo] : query) {
        if (auto text = assets->modify(foo.handle)) {
            auto now = std::chrono::system_clock::now();
            std::println(
                "{:%F %T} - {}",
                std::chrono::floor<std::chrono::seconds>(now),
                text->text()
            );
            text->set_text(text->text() + "!");
        }
    }
    using namespace std::chrono_literals;
    std::this_thread::sleep_for(1s);
}

void check_events(EventReader<AssetEvent<TextAsset>> event_reader) {
    while (auto event = event_reader.next()) {
        switch (event->type) {
            case AssetEventType::Added:
                std::println("Asset added: {}", event->id);
                break;
            case AssetEventType::Modified:
                std::println("Asset modified: {}", event->id);
                break;
            case AssetEventType::Removed:
                std::println("Asset removed: {}", event->id);
                break;
            case AssetEventType::Failed:
                std::println("Asset failed: {}", event->id);
                break;
        }
    }
}

int main() {
    App()
        .add_plugin<AssetsPlugin>()
        .add_plugin<TextAssetPlugin>()
        .add_systems(PreStartUp, startup)
        .add_systems(Update, update, check_events)
        .run();

    return 0;
}
