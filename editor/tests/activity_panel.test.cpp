#include "editor/activity_panel.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace fei::editor;

TEST_CASE("Closed activity panel skips drawing", "[editor][activity-panel]") {
    ActivityPanel panel;
    ActivityLog activity;
    ExternalAgentStatus agent;

    REQUIRE(panel.is_open());
    panel.set_open(false);
    panel.draw(activity, agent, 0);

    REQUIRE_FALSE(panel.is_open());
}
