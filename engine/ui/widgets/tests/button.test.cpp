#include "ui_widgets/button.hpp"

#include "app/app.hpp"
#include "ecs/world.hpp"
#include "input_focus/focus.hpp"
#include "ui/plugin.hpp"
#include "ui_widgets/plugin.hpp"
#include "input/input.hpp"
#include "window/window.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace fei;

namespace {

struct ButtonWorld {
    World world;
    Entity button;

    ButtonWorld() {
        world.add_resource(
            Window {.glfw_window = nullptr, .width = 200, .height = 120}
        );
        world.add_resource(MouseInput {});
        world.add_resource(KeyInput {});
        world.add_resource(Events<ui_widgets::Activate> {});
        world.add_resource(input_focus::InputFocus {});
        world.add_resource(input_focus::InputFocusVisible {});

        button = world.entity();
        world.add_component(button, ui::Node {});
        world.add_component(
            button,
            ui::ComputedNode {
                .position = {25.0f, 25.0f},
                .size = {50.0f, 50.0f},
            }
        );
        world.add_component(button, ui::Interaction::None);
        world.add_component(button, ui_widgets::Button {});
        world.add_component(button, input_focus::TabIndex {});
        world.add_resource(ui::Stack {.nodes = {button}});
    }

    void update() {
        world.run_system_once(ui::update_interactions);
        world.run_system_once(ui_widgets::update_buttons);
    }

    MouseInput& mouse() { return world.resource<MouseInput>(); }
    KeyInput& keyboard() { return world.resource<KeyInput>(); }

    const Events<ui_widgets::Activate>& activations() const {
        return world.resource<Events<ui_widgets::Activate>>();
    }
};

} // namespace

TEST_CASE(
    "Button activates on release over the pressed widget",
    "[ui_widgets][button]"
) {
    ButtonWorld test;
    test.mouse().set_position({50.0f, 50.0f});
    test.mouse().press(MouseButton::Left);

    test.update();

    CHECK(test.world.has_component<ui::Pressed>(test.button));
    CHECK(
        test.world.get_component<ui::Interaction>(test.button) ==
        ui::Interaction::Pressed
    );

    test.mouse().clear();
    test.mouse().release(MouseButton::Left);
    test.update();

    const auto event = test.activations().get_event(0);
    REQUIRE(event);
    CHECK(event->event.entity == test.button);
    CHECK(test.activations().size() == 1);
    CHECK_FALSE(test.world.has_component<ui::Pressed>(test.button));
    CHECK(
        test.world.get_component<ui::Interaction>(test.button) ==
        ui::Interaction::Hovered
    );
}

TEST_CASE(
    "Button activates from focused keyboard input",
    "[ui_widgets][button]"
) {
    ButtonWorld test;
    test.world.resource<input_focus::InputFocus>().set(test.button);
    test.keyboard().press(KeyCode::Enter);

    test.update();

    const auto event = test.activations().get_event(0);
    REQUIRE(event);
    CHECK(event->event.entity == test.button);

    test.keyboard().clear();
    test.keyboard().press(KeyCode::Enter);
    test.update();
    CHECK(test.activations().size() == 1);
}

TEST_CASE("Button activates from Space when focused", "[ui_widgets][button]") {
    ButtonWorld test;
    test.world.resource<input_focus::InputFocus>().set(test.button);
    test.keyboard().press(KeyCode::Space);

    test.update();

    const auto event = test.activations().get_event(0);
    REQUIRE(event);
    CHECK(event->event.entity == test.button);
}

TEST_CASE(
    "Disabled Button ignores pointer and keyboard activation",
    "[ui_widgets][button]"
) {
    ButtonWorld test;
    test.world.add_component(test.button, ui::InteractionDisabled {});
    test.world.resource<input_focus::InputFocus>().set(test.button);
    test.mouse().set_position({50.0f, 50.0f});
    test.mouse().press(MouseButton::Left);
    test.keyboard().press(KeyCode::Enter);

    test.update();

    CHECK(test.activations().size() == 0);
    CHECK_FALSE(test.world.has_component<ui::Pressed>(test.button));
    CHECK(
        test.world.get_component<ui::Interaction>(test.button) ==
        ui::Interaction::None
    );

    test.mouse().clear();
    test.mouse().release(MouseButton::Left);
    test.keyboard().clear();
    test.update();
    CHECK(test.activations().size() == 0);
}

TEST_CASE(
    "Button does not activate when released outside",
    "[ui_widgets][button]"
) {
    ButtonWorld test;
    test.mouse().set_position({50.0f, 50.0f});
    test.mouse().press(MouseButton::Left);
    test.update();

    test.mouse().clear();
    test.mouse().press(MouseButton::Left);
    test.mouse().set_position({150.0f, 110.0f});
    test.update();

    test.mouse().clear();
    test.mouse().release(MouseButton::Left);
    test.update();

    CHECK(test.activations().size() == 0);
    CHECK_FALSE(test.world.has_component<ui::Pressed>(test.button));
}

TEST_CASE(
    "Button does not activate through a blocking node",
    "[ui_widgets][button]"
) {
    ButtonWorld test;
    test.mouse().set_position({50.0f, 50.0f});
    test.mouse().press(MouseButton::Left);
    test.update();

    const auto blocker = test.world.entity();
    test.world.add_component(blocker, ui::Node {});
    test.world.add_component(
        blocker,
        ui::ComputedNode {
            .position = {25.0f, 25.0f},
            .size = {50.0f, 50.0f},
        }
    );
    test.world.add_component(blocker, ui::Interaction::None);
    test.world.resource<ui::Stack>().nodes.push_back(blocker);

    test.mouse().clear();
    test.mouse().release(MouseButton::Left);
    test.update();

    CHECK(test.activations().size() == 0);
    CHECK_FALSE(test.world.has_component<ui::Pressed>(test.button));
    CHECK(
        test.world.get_component<ui::Interaction>(blocker) ==
        ui::Interaction::Hovered
    );
}

TEST_CASE(
    "ActivateOnPress Button activates immediately",
    "[ui_widgets][button]"
) {
    ButtonWorld test;
    test.world.add_component(test.button, ui_widgets::ActivateOnPress {});
    test.mouse().set_position({50.0f, 50.0f});
    test.mouse().press(MouseButton::Left);

    test.update();

    const auto event = test.activations().get_event(0);
    REQUIRE(event);
    CHECK(event->event.entity == test.button);

    test.mouse().clear();
    test.mouse().release(MouseButton::Left);
    test.update();
    CHECK(test.activations().size() == 1);
}

TEST_CASE(
    "ButtonPlugin inserts required UI components",
    "[ui_widgets][button]"
) {
    App app;
    app.add_resource(
        Window {.glfw_window = nullptr, .width = 200, .height = 120}
    );
    app.add_plugin<ui_widgets::ButtonPlugin>();
    app.finish();

    const auto entity = app.world().entity();
    app.world().add_component(entity, ui_widgets::Button {});
    app.world().sort_systems();
    app.run_schedule(PostUpdate);

    CHECK(app.world().has_component<ui::Node>(entity));
    CHECK(app.world().has_component<ui::Interaction>(entity));
    CHECK(app.world().has_component<ui::FocusPolicy>(entity));
    CHECK(
        app.world().get_component<ui::Interaction>(entity) ==
        ui::Interaction::None
    );
    CHECK(
        app.world().get_component<ui::FocusPolicy>(entity) ==
        ui::FocusPolicy::Block
    );
}
