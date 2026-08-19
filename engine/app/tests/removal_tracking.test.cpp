#include "app/app.hpp"
#include "ecs/removed_components.hpp"

#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <vector>

using namespace fei;

namespace {

struct RemovalMarker {};

struct RemovalTrace {
    std::vector<Entity> entities;
};

void record_removals(
    RemovedComponents<RemovalMarker> removed,
    ResRW<RemovalTrace> trace
) {
    while (auto entity = removed.next()) {
        trace->entities.push_back(*entity);
    }
}

} // namespace

TEST_CASE(
    "App retains removals for systems that already ran this update",
    "[app][change-detection][removed]"
) {
    Registry::instance().register_type<RemovalMarker>();
    Registry::instance().register_type<RemovalTrace>();

    App app;
    app.add_resource(RemovalTrace {});
    const auto entity = app.world().entity();
    app.world().add_component(entity, RemovalMarker {});

    auto removed = std::make_shared<bool>(false);
    app.add_systems(
        Update,
        chain(record_removals, [entity, removed](Commands commands) {
            if (!*removed) {
                commands.entity(entity).remove<RemovalMarker>();
                *removed = true;
            }
        })
    );

    app.update();
    REQUIRE(app.resource<RemovalTrace>().entities.empty());

    app.update();
    REQUIRE(app.resource<RemovalTrace>().entities == std::vector {entity});
}
