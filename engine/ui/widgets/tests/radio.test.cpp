#include "ui_widgets/radio.hpp"

#include "app/app.hpp"
#include "ecs/world.hpp"
#include "input/input.hpp"
#include "input_focus/focus.hpp"
#include "input_focus/tab_navigation.hpp"
#include "ui/plugin.hpp"
#include "ui_widgets/plugin.hpp"
#include "ui_widgets/value_change.hpp"
#include "window/window.hpp"

#include <array>
#include <catch2/catch_test_macros.hpp>

using namespace fei;

namespace {

struct RadioWorld {
    World world;
    Entity group;
    std::array<Entity, 3> buttons;

    RadioWorld() {
        world.add_resource(Window {.width = 240, .height = 120});
        world.add_resource(MouseInput {});
        world.add_resource(KeyInput {});
        world.add_resource(Events<ui_widgets::ValueChange<bool>> {});
        world.add_resource(Events<ui_widgets::ValueChange<Entity>> {});
        world.add_resource(input_focus::InputFocus {});
        world.add_resource(input_focus::InputFocusVisible {});

        group = world.entity();
        world.add_component(group, ui::Node {});
        world.add_component(group, ui_widgets::RadioGroup {});
        world.add_component(group, input_focus::TabIndex {});

        for (std::size_t index = 0; index < buttons.size(); ++index) {
            const auto button = world.entity();
            buttons[index] = button;
            world.add_component(button, ui::Node {});
            world.add_component(
                button,
                ui::ComputedNode {
                    .position =
                        {10.0f + 60.0f * static_cast<float>(index), 20.0f},
                    .size = {40.0f, 40.0f},
                }
            );
            world.add_component(button, ui::Interaction::None);
            world.add_component(button, ui_widgets::RadioButton {});
            world.set_parent(button, group);
        }
        world.add_resource(
            ui::Stack {
                .nodes = {group, buttons[0], buttons[1], buttons[2]},
            }
        );
    }

    void update() {
        world.run_system_once(ui::update_interactions);
        world.run_system_once(ui_widgets::update_radio_buttons);
        world.run_system_once(ui_widgets::navigate_radio_groups);
        world.run_system_once(ui_widgets::propagate_radio_changes);
    }

    void apply_selection() {
        world.run_system_once(ui_widgets::radio_self_update);
    }

    MouseInput& mouse() { return world.resource<MouseInput>(); }
    KeyInput& keyboard() { return world.resource<KeyInput>(); }

    const Events<ui_widgets::ValueChange<bool>>& button_changes() const {
        return world.resource<Events<ui_widgets::ValueChange<bool>>>();
    }

    const Events<ui_widgets::ValueChange<Entity>>& group_changes() const {
        return world.resource<Events<ui_widgets::ValueChange<Entity>>>();
    }
};

} // namespace

TEST_CASE(
    "RadioButton click selects through its RadioGroup",
    "[ui_widgets][radio]"
) {
    RadioWorld test;
    test.world.add_component(test.buttons[0], ui::Checked {});
    test.mouse().set_position({90.0f, 40.0f});
    test.mouse().press(MouseButton::Left);
    test.update();

    CHECK(test.world.has_component<ui::Pressed>(test.buttons[1]));
    const auto focus = test.world.resource<input_focus::InputFocus>().get();
    REQUIRE(focus);
    CHECK(*focus == test.group);

    test.mouse().clear();
    test.mouse().release(MouseButton::Left);
    test.update();

    const auto button_change = test.button_changes().get_event(0);
    REQUIRE(button_change);
    CHECK(button_change->event.source == test.buttons[1]);
    CHECK(button_change->event.value);

    const auto group_change = test.group_changes().get_event(0);
    REQUIRE(group_change);
    CHECK(group_change->event.source == test.group);
    CHECK(group_change->event.value == test.buttons[1]);
    CHECK(group_change->event.is_final);

    test.apply_selection();
    CHECK_FALSE(test.world.has_component<ui::Checked>(test.buttons[0]));
    CHECK(test.world.has_component<ui::Checked>(test.buttons[1]));
    CHECK_FALSE(test.world.has_component<ui::Checked>(test.buttons[2]));
}

TEST_CASE(
    "Checked RadioButton does not emit a redundant selection",
    "[ui_widgets][radio]"
) {
    RadioWorld test;
    test.world.add_component(test.buttons[0], ui::Checked {});
    test.mouse().set_position({30.0f, 40.0f});
    test.mouse().press(MouseButton::Left);
    test.update();
    test.mouse().clear();
    test.mouse().release(MouseButton::Left);
    test.update();

    CHECK(test.button_changes().size() == 0);
    CHECK(test.group_changes().size() == 0);
}

TEST_CASE(
    "RadioGroup arrow navigation skips disabled buttons and wraps",
    "[ui_widgets][radio]"
) {
    RadioWorld test;
    test.world.add_component(test.buttons[0], ui::Checked {});
    test.world.add_component(test.buttons[1], ui::InteractionDisabled {});
    test.world.resource<input_focus::InputFocus>().set(test.group);
    test.keyboard().press(KeyCode::Right);

    test.update();

    const auto first_change = test.group_changes().get_event(0);
    REQUIRE(first_change);
    CHECK(first_change->event.value == test.buttons[2]);
    test.apply_selection();
    CHECK(test.world.has_component<ui::Checked>(test.buttons[2]));

    test.world.resource<Events<ui_widgets::ValueChange<bool>>>().clear();
    test.world.resource<Events<ui_widgets::ValueChange<Entity>>>().clear();
    test.keyboard().clear();
    test.keyboard().press(KeyCode::Down);
    test.update();

    const auto wrapped = test.group_changes().get_event(1);
    REQUIRE(wrapped);
    CHECK(wrapped->event.value == test.buttons[0]);
}

TEST_CASE(
    "RadioGroup previous navigation starts from the last button",
    "[ui_widgets][radio]"
) {
    RadioWorld test;
    test.world.resource<input_focus::InputFocus>().set(test.group);
    test.keyboard().press(KeyCode::Up);

    test.update();

    const auto group_change = test.group_changes().get_event(0);
    REQUIRE(group_change);
    CHECK(group_change->event.value == test.buttons[2]);
}

TEST_CASE(
    "Standalone focusable RadioButton emits only its boolean change",
    "[ui_widgets][radio]"
) {
    RadioWorld test;
    const auto standalone = test.world.entity();
    test.world.add_component(standalone, ui::Node {});
    test.world.add_component(standalone, ui::Interaction::None);
    test.world.add_component(standalone, ui_widgets::RadioButton {});
    test.world.add_component(standalone, input_focus::TabIndex {});
    test.world.resource<input_focus::InputFocus>().set(standalone);
    test.keyboard().press(KeyCode::Space);

    test.update();

    const auto button_change = test.button_changes().get_event(0);
    REQUIRE(button_change);
    CHECK(button_change->event.source == standalone);
    CHECK(button_change->event.value);
    CHECK(test.group_changes().size() == 0);
}

TEST_CASE(
    "Disabled RadioButton ignores pointer and keyboard selection",
    "[ui_widgets][radio]"
) {
    RadioWorld test;
    test.world.add_component(test.buttons[0], ui::InteractionDisabled {});
    test.world.resource<input_focus::InputFocus>().set(test.buttons[0]);
    test.keyboard().press(KeyCode::Enter);
    test.mouse().set_position({30.0f, 40.0f});
    test.mouse().press(MouseButton::Left);

    test.update();

    CHECK(test.button_changes().size() == 0);
    CHECK(test.group_changes().size() == 0);
    CHECK_FALSE(test.world.has_component<ui::Pressed>(test.buttons[0]));
}

TEST_CASE(
    "radio_self_update ignores selections outside the source group",
    "[ui_widgets][radio]"
) {
    RadioWorld test;
    const auto outside = test.world.entity();
    test.world.add_component(outside, ui_widgets::RadioButton {});
    test.world.resource<Events<ui_widgets::ValueChange<Entity>>>().send(
        {.source = test.group, .value = outside, .is_final = true}
    );

    test.apply_selection();

    CHECK_FALSE(test.world.has_component<ui::Checked>(outside));
}

TEST_CASE(
    "radio_self_update uses the last selection in one frame",
    "[ui_widgets][radio]"
) {
    RadioWorld test;
    test.world.add_component(test.buttons[0], ui::Checked {});
    auto& changes =
        test.world.resource<Events<ui_widgets::ValueChange<Entity>>>();
    changes.send(
        {.source = test.group, .value = test.buttons[1], .is_final = true}
    );
    changes.send(
        {.source = test.group, .value = test.buttons[2], .is_final = true}
    );

    test.apply_selection();

    CHECK_FALSE(test.world.has_component<ui::Checked>(test.buttons[0]));
    CHECK_FALSE(test.world.has_component<ui::Checked>(test.buttons[1]));
    CHECK(test.world.has_component<ui::Checked>(test.buttons[2]));
}

TEST_CASE(
    "RadioGroupPlugin inserts required RadioButton UI components",
    "[ui_widgets][radio]"
) {
    App app;
    app.add_resource(Window {.width = 200, .height = 120});
    app.add_plugin<ui_widgets::RadioGroupPlugin>();
    app.finish();

    const auto entity = app.world().entity();
    app.world().add_component(entity, ui_widgets::RadioButton {});
    app.world().sort_systems();
    app.run_schedule(PostUpdate);

    CHECK(app.world().has_component<ui::Node>(entity));
    CHECK(app.world().has_component<ui::Interaction>(entity));
    CHECK(app.world().has_component<ui::FocusPolicy>(entity));
    CHECK(app.world().has_component<ui::Checkable>(entity));
    CHECK_FALSE(app.world().has_component<input_focus::TabIndex>(entity));
}
