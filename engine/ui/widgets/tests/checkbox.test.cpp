#include "ui_widgets/checkbox.hpp"

#include "app/app.hpp"
#include "ecs/world.hpp"
#include "input/input.hpp"
#include "input_focus/focus.hpp"
#include "ui/plugin.hpp"
#include "ui_widgets/plugin.hpp"
#include "ui_widgets/value_change.hpp"
#include "window/window.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace fei;

namespace {

struct CheckboxWorld {
    World world;
    Entity checkbox;

    CheckboxWorld() {
        world.add_resource(Window {.width = 200, .height = 120});
        world.add_resource(MouseInput {});
        world.add_resource(KeyInput {});
        world.add_resource(Events<ui_widgets::ValueChange<bool>> {});
        world.add_resource(Events<ui_widgets::SetChecked> {});
        world.add_resource(Events<ui_widgets::ToggleChecked> {});
        world.add_resource(input_focus::InputFocus {});
        world.add_resource(input_focus::InputFocusVisible {});

        checkbox = world.entity();
        world.add_component(checkbox, ui::Node {});
        world.add_component(
            checkbox,
            ui::ComputedNode {
                .position = {25.0f, 25.0f},
                .size = {50.0f, 50.0f},
            }
        );
        world.add_component(checkbox, ui::Interaction::None);
        world.add_component(checkbox, ui_widgets::Checkbox {});
        world.add_component(checkbox, input_focus::TabIndex {});
        world.add_resource(ui::Stack {.nodes = {checkbox}});
    }

    void update() {
        world.run_system_once(ui::update_interactions);
        world.run_system_once(ui_widgets::update_checkboxes);
    }

    MouseInput& mouse() { return world.resource<MouseInput>(); }
    KeyInput& keyboard() { return world.resource<KeyInput>(); }

    const Events<ui_widgets::ValueChange<bool>>& changes() const {
        return world.resource<Events<ui_widgets::ValueChange<bool>>>();
    }
};

} // namespace

TEST_CASE(
    "Checkbox requests checked state on pointer click",
    "[ui_widgets][checkbox]"
) {
    CheckboxWorld test;
    test.mouse().set_position({50.0f, 50.0f});
    test.mouse().press(MouseButton::Left);
    test.update();

    CHECK(test.world.has_component<ui::Pressed>(test.checkbox));

    test.mouse().clear();
    test.mouse().release(MouseButton::Left);
    test.update();

    const auto event = test.changes().get_event(0);
    REQUIRE(event);
    CHECK(event->event.source == test.checkbox);
    CHECK(event->event.value);
    CHECK(event->event.is_final);
    CHECK_FALSE(test.world.has_component<ui::Checked>(test.checkbox));
    CHECK_FALSE(test.world.has_component<ui::Pressed>(test.checkbox));
}

TEST_CASE(
    "Checked Checkbox requests unchecked state from keyboard",
    "[ui_widgets][checkbox]"
) {
    CheckboxWorld test;
    test.world.add_component(test.checkbox, ui::Checked {});
    test.world.resource<input_focus::InputFocus>().set(test.checkbox);
    test.keyboard().press(KeyCode::Space);

    test.update();

    const auto event = test.changes().get_event(0);
    REQUIRE(event);
    CHECK(event->event.source == test.checkbox);
    CHECK_FALSE(event->event.value);
    CHECK(test.world.has_component<ui::Checked>(test.checkbox));
}

TEST_CASE(
    "Checkbox activates from Enter when focused",
    "[ui_widgets][checkbox]"
) {
    CheckboxWorld test;
    test.world.resource<input_focus::InputFocus>().set(test.checkbox);
    test.keyboard().press(KeyCode::Enter);

    test.update();

    const auto event = test.changes().get_event(0);
    REQUIRE(event);
    CHECK(event->event.value);
}

TEST_CASE(
    "Disabled Checkbox does not request value changes",
    "[ui_widgets][checkbox]"
) {
    CheckboxWorld test;
    test.world.add_component(test.checkbox, ui::InteractionDisabled {});
    test.world.resource<input_focus::InputFocus>().set(test.checkbox);
    test.mouse().set_position({50.0f, 50.0f});
    test.mouse().press(MouseButton::Left);
    test.keyboard().press(KeyCode::Enter);

    test.update();

    CHECK(test.changes().size() == 0);
    CHECK_FALSE(test.world.has_component<ui::Pressed>(test.checkbox));
    CHECK(
        test.world.get_component<ui::Interaction>(test.checkbox) ==
        ui::Interaction::None
    );
}

TEST_CASE(
    "Checkbox does not change when released outside",
    "[ui_widgets][checkbox]"
) {
    CheckboxWorld test;
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

    CHECK(test.changes().size() == 0);
    CHECK_FALSE(test.world.has_component<ui::Pressed>(test.checkbox));
}

TEST_CASE(
    "SetChecked requests an explicit Checkbox value",
    "[ui_widgets][checkbox]"
) {
    CheckboxWorld test;
    test.world.resource<Events<ui_widgets::SetChecked>>().send(
        {.entity = test.checkbox, .checked = true}
    );

    test.update();

    const auto event = test.changes().get_event(0);
    REQUIRE(event);
    CHECK(event->event.source == test.checkbox);
    CHECK(event->event.value);
    CHECK(event->event.is_final);
    CHECK_FALSE(test.world.has_component<ui::Checked>(test.checkbox));
}

TEST_CASE(
    "SetChecked suppresses redundant value changes",
    "[ui_widgets][checkbox]"
) {
    CheckboxWorld test;
    test.world.add_component(test.checkbox, ui::Checked {});
    test.world.resource<Events<ui_widgets::SetChecked>>().send(
        {.entity = test.checkbox, .checked = true}
    );

    test.update();

    CHECK(test.changes().size() == 0);
}

TEST_CASE(
    "ToggleChecked requests the inverse Checkbox value",
    "[ui_widgets][checkbox]"
) {
    CheckboxWorld test;
    test.world.add_component(test.checkbox, ui::Checked {});
    test.world.resource<Events<ui_widgets::ToggleChecked>>().send(
        {.entity = test.checkbox}
    );

    test.update();

    const auto event = test.changes().get_event(0);
    REQUIRE(event);
    CHECK(event->event.source == test.checkbox);
    CHECK_FALSE(event->event.value);
}

TEST_CASE(
    "Disabled Checkbox ignores external control events",
    "[ui_widgets][checkbox]"
) {
    CheckboxWorld test;
    test.world.add_component(test.checkbox, ui::InteractionDisabled {});
    test.world.resource<Events<ui_widgets::SetChecked>>().send(
        {.entity = test.checkbox, .checked = true}
    );
    test.world.resource<Events<ui_widgets::ToggleChecked>>().send(
        {.entity = test.checkbox}
    );

    test.update();

    CHECK(test.changes().size() == 0);
}

TEST_CASE(
    "checkbox_self_update applies requested state",
    "[ui_widgets][checkbox]"
) {
    CheckboxWorld test;
    test.world.resource<Events<ui_widgets::ValueChange<bool>>>().send(
        {.source = test.checkbox, .value = true, .is_final = true}
    );

    test.world.run_system_once(ui_widgets::checkbox_self_update);

    CHECK(test.world.has_component<ui::Checked>(test.checkbox));
}

TEST_CASE(
    "checkbox_self_update removes checked state",
    "[ui_widgets][checkbox]"
) {
    CheckboxWorld test;
    test.world.add_component(test.checkbox, ui::Checked {});
    test.world.resource<Events<ui_widgets::ValueChange<bool>>>().send(
        {.source = test.checkbox, .value = false, .is_final = true}
    );

    test.world.run_system_once(ui_widgets::checkbox_self_update);

    CHECK_FALSE(test.world.has_component<ui::Checked>(test.checkbox));
}

TEST_CASE(
    "CheckboxPlugin inserts required UI and checkable components",
    "[ui_widgets][checkbox]"
) {
    App app;
    app.add_resource(Window {.width = 200, .height = 120});
    app.add_plugin<ui_widgets::CheckboxPlugin>();
    app.finish();

    const auto entity = app.world().entity();
    app.world().add_component(entity, ui_widgets::Checkbox {});
    app.world().sort_systems();
    app.run_schedule(PostUpdate);

    CHECK(app.world().has_component<ui::Node>(entity));
    CHECK(app.world().has_component<ui::Interaction>(entity));
    CHECK(app.world().has_component<ui::FocusPolicy>(entity));
    CHECK(app.world().has_component<ui::Checkable>(entity));
    CHECK_FALSE(app.world().has_component<ui::Checked>(entity));
}
