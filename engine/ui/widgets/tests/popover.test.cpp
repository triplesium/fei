#include "ui_widgets/popover.hpp"

#include "ecs/world.hpp"
#include "ui/node.hpp"
#include "ui_widgets/plugin.hpp"
#include "window/window.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace fei;

namespace {

struct PopoverWorld {
    World world;
    Entity root;
    Entity anchor;
    Entity popup;

    PopoverWorld(
        Vector2 anchor_position,
        ui_widgets::PopoverPlacement placement = {}
    ) {
        world.add_resource(Window {.width = 300, .height = 200});
        root = world.entity();
        world.add_component(root, ui::Node {});
        world.add_component(
            root,
            ui::ComputedNode {
                .position = {0.0f, 0.0f},
                .size = {300.0f, 200.0f},
                .content_size = {300.0f, 200.0f},
                .content_position = {0.0f, 0.0f},
            }
        );
        anchor = world.entity();
        world.add_component(anchor, ui::Node {});
        world.add_component(
            anchor,
            ui::ComputedNode {
                .position = anchor_position,
                .size = {40.0f, 20.0f},
            }
        );
        world.set_parent(anchor, root);
        popup = world.entity();
        world.add_component(
            popup,
            ui::Node {
                .position_type = ui::PositionType::Absolute,
                .width = ui::px(80.0f),
                .height = ui::px(50.0f),
            }
        );
        world.add_component(popup, ui::ComputedNode {.size = {80.0f, 50.0f}});
        world.add_component(
            popup,
            ui_widgets::Popover {.anchor = anchor, .placement = placement}
        );
        world.set_parent(popup, root);
    }

    void update() { world.run_system_once(ui_widgets::update_popovers); }
};

} // namespace

TEST_CASE("Popover is placed below its anchor", "[ui_widgets][popover]") {
    PopoverWorld test({50.0f, 40.0f});

    test.update();

    const auto& node = test.world.get_component<ui::Node>(test.popup);
    CHECK(node.left == ui::px(50.0f));
    CHECK(node.top == ui::px(64.0f));
}

TEST_CASE(
    "Popover flips when its preferred side overflows",
    "[ui_widgets][popover]"
) {
    PopoverWorld test({50.0f, 170.0f});

    test.update();

    const auto& node = test.world.get_component<ui::Node>(test.popup);
    CHECK(node.left == ui::px(50.0f));
    CHECK(node.top == ui::px(116.0f));
}

TEST_CASE(
    "Popover cross-axis position is clamped to the viewport",
    "[ui_widgets][popover]"
) {
    PopoverWorld test(
        {280.0f, 40.0f},
        {.side = ui_widgets::PopoverSide::Bottom,
         .align = ui_widgets::PopoverAlign::End,
         .viewport_margin = 8.0f}
    );

    test.update();

    const auto& node = test.world.get_component<ui::Node>(test.popup);
    CHECK(node.left == ui::px(212.0f));
}
