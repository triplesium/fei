#include "input_focus/tab_navigation.hpp"

#include "app/app.hpp"
#include "ecs/world.hpp"
#include "input_focus/focus.hpp"
#include "input_focus/plugin.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace ets;

namespace {

struct NavigationWorld {
    World world;

    NavigationWorld() {
        world.add_resource(KeyInput {});
        world.add_resource(input_focus::InputFocus {});
        world.add_resource(input_focus::InputFocusVisible {});
    }

    void update() { world.run_system_once(input_focus::navigate_focus); }

    void release_keys() {
        world.resource<KeyInput>().clear();
        update();
        world.resource<KeyInput>().clear();
    }

    void tab(bool shift = false) {
        auto& keyboard = world.resource<KeyInput>();
        keyboard.press(KeyCode::Tab);
        if (shift) {
            keyboard.press(KeyCode::LeftShift);
        }
        update();
        release_keys();
    }

    Entity focused() const {
        const auto focus = world.resource<input_focus::InputFocus>().get();
        REQUIRE(focus);
        return *focus;
    }
};

} // namespace

TEST_CASE(
    "Tab navigation follows group index and hierarchy order",
    "[input_focus][tab]"
) {
    NavigationWorld test;
    const auto second_group = test.world.entity();
    const auto first_group = test.world.entity();
    test.world.add_component(second_group, input_focus::TabGroup::ordered(1));
    test.world.add_component(first_group, input_focus::TabGroup {});

    const auto later_index = test.world.entity();
    const auto first_in_tree = test.world.entity();
    const auto same_index = test.world.entity();
    const auto second_group_item = test.world.entity();
    test.world.add_component(later_index, input_focus::TabIndex {.index = 2});
    test.world.add_component(first_in_tree, input_focus::TabIndex {});
    test.world.add_component(same_index, input_focus::TabIndex {});
    test.world.add_component(second_group_item, input_focus::TabIndex {});
    test.world.set_parent(later_index, first_group);
    test.world.set_parent(first_in_tree, first_group);
    test.world.set_parent(same_index, first_group);
    test.world.set_parent(second_group_item, second_group);

    test.tab();
    CHECK(test.focused() == first_in_tree);
    test.tab();
    CHECK(test.focused() == same_index);
    test.tab();
    CHECK(test.focused() == later_index);
    test.tab();
    CHECK(test.focused() == second_group_item);
    test.tab();
    CHECK(test.focused() == first_in_tree);
}

TEST_CASE(
    "InputFocusPlugin navigates after input events",
    "[input_focus][tab]"
) {
    App app;
    app.add_plugin<input_focus::InputFocusPlugin>();
    app.startup();

    const auto group = app.world().entity();
    const auto item = app.world().entity();
    app.world().add_component(group, input_focus::TabGroup {});
    app.world().add_component(item, input_focus::TabIndex {});
    app.world().set_parent(item, group);
    app.resource<Events<KeyEvent>>().send(
        KeyEvent {.key_code = KeyCode::Tab, .state = KeyState::Pressed}
    );

    app.update();

    REQUIRE(app.resource<input_focus::InputFocus>().get());
    CHECK(*app.resource<input_focus::InputFocus>().get() == item);
    app.shutdown();
}

TEST_CASE("Shift Tab navigates backward and wraps", "[input_focus][tab]") {
    NavigationWorld test;
    const auto group = test.world.entity();
    test.world.add_component(group, input_focus::TabGroup {});
    const auto first = test.world.entity();
    const auto second = test.world.entity();
    test.world.add_component(first, input_focus::TabIndex {});
    test.world.add_component(second, input_focus::TabIndex {});
    test.world.set_parent(first, group);
    test.world.set_parent(second, group);

    test.tab(true);
    CHECK(test.focused() == second);
    test.tab(true);
    CHECK(test.focused() == first);
    CHECK(test.world.resource<input_focus::InputFocusVisible>().visible);
}

TEST_CASE(
    "Modal TabGroup traps navigation and negative indices are skipped",
    "[input_focus][tab]"
) {
    NavigationWorld test;
    const auto regular_group = test.world.entity();
    const auto modal_group = test.world.entity();
    test.world.add_component(regular_group, input_focus::TabGroup {});
    test.world.add_component(modal_group, input_focus::TabGroup::modal_group());
    const auto regular = test.world.entity();
    const auto modal_first = test.world.entity();
    const auto skipped = test.world.entity();
    const auto modal_last = test.world.entity();
    test.world.add_component(regular, input_focus::TabIndex {});
    test.world.add_component(modal_first, input_focus::TabIndex {});
    test.world.add_component(skipped, input_focus::TabIndex {.index = -1});
    test.world.add_component(modal_last, input_focus::TabIndex {.index = 1});
    test.world.set_parent(regular, regular_group);
    test.world.set_parent(modal_first, modal_group);
    test.world.set_parent(skipped, modal_group);
    test.world.set_parent(modal_last, modal_group);
    test.world.resource<input_focus::InputFocus>().set(modal_first);

    test.tab();
    CHECK(test.focused() == modal_last);
    test.tab();
    CHECK(test.focused() == modal_first);
    CHECK(test.focused() != regular);
    CHECK(test.focused() != skipped);
}
