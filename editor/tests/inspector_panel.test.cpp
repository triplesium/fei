#include "editor/inspector_panel.hpp"

#include "ecs/world.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace fei;
using namespace fei::editor;

TEST_CASE("Closed inspector panel skips drawing", "[editor][inspector-panel]") {
    InspectorPanel panel;
    World world;
    Selection selection;
    ComponentOperations operations;
    ActivityLog activity;
    bool drew_asset = false;

    selection.asset = AssetPath("project://image.png");
    panel.set_open(false);
    panel.draw(
        InspectorPanelContext {
            .world = world,
            .selection = selection,
            .operations = operations,
            .activity = activity,
            .draw_asset = [&drew_asset](const AssetPath&) {
                drew_asset = true;
            },
        }
    );

    REQUIRE_FALSE(panel.is_open());
    REQUIRE_FALSE(drew_asset);
    REQUIRE(activity.entries().empty());
}
