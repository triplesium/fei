#include "editor/scene_panel.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace fei::editor;

TEST_CASE("Closed scene panel disables its viewport", "[editor][scene-panel]") {
    ScenePanel panel;
    SceneViewport viewport;

    REQUIRE(panel.is_open());
    panel.set_open(false);
    panel.draw(viewport, false, {});

    REQUIRE_FALSE(panel.is_open());
    REQUIRE_FALSE(viewport.visible);
}
