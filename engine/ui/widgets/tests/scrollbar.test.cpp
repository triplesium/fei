#include "ui_widgets/scrollbar.hpp"

#include "app/app.hpp"
#include "ecs/world.hpp"
#include "ui/plugin.hpp"
#include "ui_widgets/plugin.hpp"
#include "window/input.hpp"
#include "window/window.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace fei;

namespace {

struct ScrollbarWorld {
    World world;
    Entity target;
    Entity scrollbar;
    Entity thumb;

    ScrollbarWorld(
        ui_widgets::ControlOrientation orientation =
            ui_widgets::ControlOrientation::Vertical,
        float min_thumb_length = 8.0f
    ) {
        world.add_resource(
            Window {.glfw_window = nullptr, .width = 200, .height = 160}
        );
        world.add_resource(MouseInput {});

        target = world.entity();
        world.add_component(
            target,
            ui::ScrollPosition {.offset = {0.0f, 150.0f}}
        );
        world.add_component(
            target,
            ui::ComputedNode {
                .content_size = {100.0f, 100.0f},
                .scroll_content_size = {400.0f, 400.0f},
            }
        );

        scrollbar = world.entity();
        world.add_component(
            scrollbar,
            ui_widgets::Scrollbar::new_scrollbar(
                target,
                orientation,
                min_thumb_length
            )
        );
        world.add_component(scrollbar, ui::Interaction::None);
        world.add_component(
            scrollbar,
            ui::ComputedNode {
                .position = {10.0f, 10.0f},
                .size =
                    orientation == ui_widgets::ControlOrientation::Vertical ?
                        Vector2 {12.0f, 100.0f} :
                        Vector2 {100.0f, 12.0f},
            }
        );

        thumb = world.entity();
        world.add_component(thumb, ui_widgets::ScrollbarThumb {});
        world.add_component(thumb, ui::Node {});
        world.add_component(thumb, ui::Interaction::None);
        world.add_component(thumb, ui_widgets::ScrollbarDragState {});
        world.add_component(
            thumb,
            ui::ComputedNode {
                .position =
                    orientation == ui_widgets::ControlOrientation::Vertical ?
                        Vector2 {10.0f, 47.5f} :
                        Vector2 {47.5f, 10.0f},
                .size =
                    orientation == ui_widgets::ControlOrientation::Vertical ?
                        Vector2 {12.0f, 25.0f} :
                        Vector2 {25.0f, 12.0f},
            }
        );
        world.set_parent(thumb, scrollbar);
    }

    void update() { world.run_system_once(ui_widgets::update_scrollbars); }

    MouseInput& mouse() { return world.resource<MouseInput>(); }

    void press(Vector2 position) {
        mouse().set_position(position);
        mouse().press(MouseButton::Left);
        world.add_component(thumb, ui::Interaction::Pressed);
        update();
    }

    void move(Vector2 position) {
        mouse().clear();
        mouse().press(MouseButton::Left);
        mouse().set_position(position);
        update();
    }

    void release(Vector2 position) {
        mouse().clear();
        mouse().set_position(position);
        world.add_component(thumb, ui::Interaction::None);
        update();
    }
};

} // namespace

TEST_CASE(
    "Scrollbar sizes and positions a vertical thumb from its target",
    "[ui_widgets][scrollbar]"
) {
    ScrollbarWorld test;

    test.update();

    const auto& node = test.world.get_component<ui::Node>(test.thumb);
    CHECK(node.position_type == ui::PositionType::Absolute);
    CHECK(node.height == ui::px(25.0f));
    CHECK(node.top == ui::px(37.5f));
}

TEST_CASE(
    "Scrollbar respects its minimum thumb length",
    "[ui_widgets][scrollbar]"
) {
    ScrollbarWorld test(ui_widgets::ControlOrientation::Vertical, 40.0f);

    test.update();

    const auto& node = test.world.get_component<ui::Node>(test.thumb);
    CHECK(node.height == ui::px(40.0f));
    CHECK(node.top == ui::px(30.0f));
}

TEST_CASE(
    "Scrollbar fills the track when content does not scroll",
    "[ui_widgets][scrollbar]"
) {
    ScrollbarWorld test;
    test.world.add_component(
        test.target,
        ui::ComputedNode {
            .content_size = {100.0f, 100.0f},
            .scroll_content_size = {100.0f, 100.0f},
        }
    );

    test.update();

    const auto& node = test.world.get_component<ui::Node>(test.thumb);
    CHECK(node.height == ui::px(100.0f));
    CHECK(node.top == ui::px(0.0f));
}

TEST_CASE(
    "Vertical Scrollbar dragging modifies target ScrollPosition",
    "[ui_widgets][scrollbar]"
) {
    ScrollbarWorld test;
    test.press({16.0f, 60.0f});
    test.move({16.0f, 80.0f});

    CHECK(
        test.world.get_component<ui::ScrollPosition>(test.target).offset.y ==
        Catch::Approx(230.0f)
    );

    test.release({16.0f, 90.0f});
    CHECK(
        test.world.get_component<ui::ScrollPosition>(test.target).offset.y ==
        Catch::Approx(270.0f)
    );
    CHECK_FALSE(test.world
                    .get_component<ui_widgets::ScrollbarDragState>(test.thumb)
                    .dragging);
}

TEST_CASE(
    "Horizontal Scrollbar updates the horizontal axis",
    "[ui_widgets][scrollbar]"
) {
    ScrollbarWorld test(ui_widgets::ControlOrientation::Horizontal);
    test.world.add_component(
        test.target,
        ui::ScrollPosition {.offset = {150.0f, 0.0f}}
    );

    test.update();
    const auto& node = test.world.get_component<ui::Node>(test.thumb);
    CHECK(node.width == ui::px(25.0f));
    CHECK(node.left == ui::px(37.5f));

    test.press({60.0f, 16.0f});
    test.move({80.0f, 16.0f});
    CHECK(
        test.world.get_component<ui::ScrollPosition>(test.target).offset.x ==
        Catch::Approx(230.0f)
    );
}

TEST_CASE(
    "Scrollbar track presses scroll by one visible page",
    "[ui_widgets][scrollbar]"
) {
    ScrollbarWorld test;

    test.mouse().set_position({16.0f, 100.0f});
    test.mouse().press(MouseButton::Left);
    test.world.add_component(test.scrollbar, ui::Interaction::Pressed);
    test.update();

    CHECK(
        test.world.get_component<ui::ScrollPosition>(test.target).offset.y ==
        Catch::Approx(250.0f)
    );
    CHECK_FALSE(test.world
                    .get_component<ui_widgets::ScrollbarDragState>(test.thumb)
                    .dragging);
}

TEST_CASE(
    "ScrollbarPlugin inserts required components",
    "[ui_widgets][scrollbar]"
) {
    App app;
    app.add_resource(
        Window {.glfw_window = nullptr, .width = 200, .height = 120}
    );
    app.add_plugin<ui_widgets::ScrollbarPlugin>();
    app.finish();

    const auto target = app.world().entity();
    app.world().add_component(target, ui::ScrollPosition {});
    const auto scrollbar = app.world().entity();
    app.world().add_component(
        scrollbar,
        ui_widgets::Scrollbar::new_scrollbar(
            target,
            ui_widgets::ControlOrientation::Vertical,
            8.0f
        )
    );
    const auto thumb = app.world().entity();
    app.world().add_component(thumb, ui_widgets::ScrollbarThumb {});
    app.world().set_parent(thumb, scrollbar);
    app.world().sort_systems();
    app.run_schedule(PostUpdate);

    CHECK(app.world().has_component<ui::Node>(scrollbar));
    CHECK(app.world().has_component<ui::Interaction>(scrollbar));
    CHECK(app.world().has_component<ui::FocusPolicy>(scrollbar));
    CHECK(app.world().has_component<ui::Node>(thumb));
    CHECK(app.world().has_component<ui::Interaction>(thumb));
    CHECK(app.world().has_component<ui_widgets::ScrollbarDragState>(thumb));
    REQUIRE(app.world().has_component<ui::FocusPolicy>(thumb));
    CHECK(
        app.world().get_component<ui::FocusPolicy>(thumb) ==
        ui::FocusPolicy::Block
    );
}
