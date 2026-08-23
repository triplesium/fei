#include "ui/interaction.hpp"

#include "ecs/world.hpp"
#include "input/input.hpp"
#include "input_focus/focus.hpp"
#include "input_focus/tab_navigation.hpp"
#include "ui/plugin.hpp"
#include "window/window.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace ets;

namespace {

struct InteractionWorld {
    World world;
    Entity bottom;
    Entity top;

    InteractionWorld() {
        world.add_resource(Window {.width = 200, .height = 120});
        world.add_resource(MouseInput {});
        world.add_resource(KeyInput {});
        world.add_resource(input_focus::InputFocus {});
        world.add_resource(input_focus::InputFocusVisible {});

        bottom = world.entity();
        top = world.entity();
        world.add_component(bottom, ui::Node {});
        world.add_component(
            bottom,
            ui::ComputedNode {
                .position = {0.0f, 0.0f},
                .size = {100.0f, 100.0f}
            }
        );
        world.add_component(bottom, ui::Interaction::None);
        world.add_component(top, ui::Node {});
        world.add_component(
            top,
            ui::ComputedNode {
                .position = {25.0f, 25.0f},
                .size = {50.0f, 50.0f},
            }
        );
        world.add_component(top, ui::Interaction::None);
        world.add_component(top, ui::RelativeCursorPosition {});
        world.add_resource(ui::Stack {.nodes = {bottom, top}});
    }

    void update() { world.run_system_once(ui::update_interactions); }

    MouseInput& mouse() { return world.resource<MouseInput>(); }
    KeyInput& keyboard() { return world.resource<KeyInput>(); }
};

} // namespace

TEST_CASE(
    "UI interaction resolves topmost focus and pass-through",
    "[ui][interaction]"
) {
    InteractionWorld test;
    test.mouse().set_position({50.0f, 50.0f});

    test.update();

    CHECK(
        test.world.get_component<ui::Interaction>(test.top) ==
        ui::Interaction::Hovered
    );
    CHECK(
        test.world.get_component<ui::Interaction>(test.bottom) ==
        ui::Interaction::None
    );
    const auto& cursor =
        test.world.get_component<ui::RelativeCursorPosition>(test.top);
    CHECK(cursor.cursor_over);
    REQUIRE(cursor.normalized);
    CHECK(cursor.normalized->x == Catch::Approx(0.0f));
    CHECK(cursor.normalized->y == Catch::Approx(0.0f));

    test.world.add_component(test.top, ui::FocusPolicy::Pass);
    test.update();

    CHECK(
        test.world.get_component<ui::Interaction>(test.top) ==
        ui::Interaction::Hovered
    );
    CHECK(
        test.world.get_component<ui::Interaction>(test.bottom) ==
        ui::Interaction::Hovered
    );
}

TEST_CASE("UI interaction honors inherited clipping", "[ui][interaction]") {
    InteractionWorld test;
    test.world.add_component(test.top, ui::FocusPolicy::Block);
    test.world.add_component(
        test.top,
        ui::CalculatedClip {
            .clip = {.min = {0.0f, 0.0f}, .max = {20.0f, 20.0f}},
        }
    );
    test.mouse().set_position({50.0f, 50.0f});

    test.update();

    CHECK(
        test.world.get_component<ui::Interaction>(test.top) ==
        ui::Interaction::None
    );
    CHECK(
        test.world.get_component<ui::Interaction>(test.bottom) ==
        ui::Interaction::Hovered
    );
    const auto& cursor =
        test.world.get_component<ui::RelativeCursorPosition>(test.top);
    CHECK_FALSE(cursor.cursor_over);
    REQUIRE(cursor.normalized);
    CHECK(cursor.normalized->x == Catch::Approx(0.0f));
    CHECK(cursor.normalized->y == Catch::Approx(0.0f));
}

TEST_CASE("UI pressed state persists until release", "[ui][interaction]") {
    InteractionWorld test;
    test.mouse().set_position({50.0f, 50.0f});
    test.mouse().press(MouseButton::Left);

    test.update();

    REQUIRE(
        test.world.get_component<ui::Interaction>(test.top) ==
        ui::Interaction::Pressed
    );
    const auto pressed_tick =
        test.world.get_component_rw<ui::Interaction>(test.top).changed_tick();

    test.mouse().clear();
    test.mouse().press(MouseButton::Left);
    test.mouse().set_position({5.0f, 5.0f});
    test.update();

    CHECK(
        test.world.get_component<ui::Interaction>(test.top) ==
        ui::Interaction::Pressed
    );
    CHECK(
        test.world.get_component_rw<ui::Interaction>(test.top).changed_tick() ==
        pressed_tick
    );

    test.mouse().clear();
    test.mouse().release(MouseButton::Left);
    test.mouse().set_position({50.0f, 50.0f});
    test.update();

    CHECK(
        test.world.get_component<ui::Interaction>(test.top) ==
        ui::Interaction::Hovered
    );
}

TEST_CASE(
    "Disabled UI interaction remains None but acquires pointer focus",
    "[ui][interaction]"
) {
    InteractionWorld test;
    test.world.add_component(test.top, ui::InteractionDisabled {});
    test.world.add_component(test.top, input_focus::TabIndex {});
    test.mouse().set_position({50.0f, 50.0f});
    test.mouse().press(MouseButton::Left);

    test.update();

    CHECK(
        test.world.get_component<ui::Interaction>(test.top) ==
        ui::Interaction::None
    );
    const auto focus = test.world.resource<input_focus::InputFocus>().get();
    REQUIRE(focus);
    CHECK(*focus == test.top);
    CHECK(
        test.world.resource<input_focus::InputFocus>().cause ==
        input_focus::FocusCause::Pointer
    );
}

TEST_CASE(
    "Disabled UI entity remains in Tab navigation",
    "[ui][interaction][tab]"
) {
    InteractionWorld test;
    const auto group = test.world.entity();
    test.world.add_component(group, input_focus::TabGroup {});
    test.world.add_component(test.top, ui::InteractionDisabled {});
    test.world.add_component(test.top, input_focus::TabIndex {});
    test.world.set_parent(test.top, group);
    test.keyboard().press(KeyCode::Tab);

    test.world.run_system_once(input_focus::navigate_focus);

    const auto focus = test.world.resource<input_focus::InputFocus>().get();
    REQUIRE(focus);
    CHECK(*focus == test.top);
    CHECK(test.world.resource<input_focus::InputFocusVisible>().visible);
}

TEST_CASE(
    "UI pointer focus hides the keyboard focus indicator",
    "[ui][interaction]"
) {
    InteractionWorld test;
    test.world.add_component(test.top, input_focus::TabIndex {});
    test.world.resource<input_focus::InputFocusVisible>().visible = true;
    test.mouse().set_position({50.0f, 50.0f});
    test.mouse().press(MouseButton::Left);

    test.update();

    const auto focus = test.world.resource<input_focus::InputFocus>().get();
    REQUIRE(focus);
    CHECK(*focus == test.top);
    CHECK(
        test.world.resource<input_focus::InputFocus>().cause ==
        input_focus::FocusCause::Pointer
    );
    CHECK_FALSE(test.world.resource<input_focus::InputFocusVisible>().visible);
}
