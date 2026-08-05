#include "editor/assets_panel.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace fei::editor;

TEST_CASE("Assets panel owns its visibility state", "[editor][assets-panel]") {
    AssetsPanel panel;

    REQUIRE(panel.is_open());
    panel.set_open(false);
    REQUIRE_FALSE(panel.is_open());
}
