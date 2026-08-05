#include "editor/hierarchy_panel.hpp"

#include "ecs/world.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace fei;
using namespace fei::editor;

TEST_CASE("Closed hierarchy panel skips drawing", "[editor][hierarchy-panel]") {
    HierarchyPanel panel;
    World world;
    Selection selection;
    ActivityLog activity;
    SceneEntityBindings bindings;

    REQUIRE(panel.is_open());
    panel.set_open(false);
    panel.draw(
        HierarchyPanelContext {
            .world = world,
            .selection = selection,
            .activity = activity,
            .bindings = bindings,
        }
    );

    REQUIRE_FALSE(panel.is_open());
    REQUIRE(world.archetypes().begin() == world.archetypes().end());
    REQUIRE(activity.entries().empty());
}
