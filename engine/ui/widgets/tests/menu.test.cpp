#include "ui_widgets/menu.hpp"

#include "ecs/world.hpp"
#include "input/input.hpp"
#include "input_focus/focus.hpp"
#include "ui/node.hpp"
#include "ui_widgets/plugin.hpp"

#include <array>
#include <catch2/catch_test_macros.hpp>

using namespace ets;

namespace {

struct MenuWorld {
    World world;
    Entity button;
    Entity popup;
    std::array<Entity, 2> items;

    MenuWorld(bool open = false) {
        world.add_resource(MouseInput {});
        world.add_resource(KeyInput {});
        world.add_resource(input_focus::InputFocus {});
        world.add_resource(input_focus::InputFocusVisible {});
        world.add_resource(Events<ui_widgets::Activate> {});
        world.add_resource(Events<ui_widgets::MenuEvent> {});

        button = world.entity();
        popup = world.entity();
        world.add_component(button, ui_widgets::MenuButton {.popup = popup});
        world.add_component(
            popup,
            ui::Node {
                .display = open ? ui::Display::Flex : ui::Display::None,
            }
        );
        world.add_component(popup, ui_widgets::MenuPopup {});
        for (auto& item : items) {
            item = world.entity();
            world.add_component(item, ui_widgets::MenuItem {});
            world.set_parent(item, popup);
        }
    }

    void update() { world.run_system_once(ui_widgets::update_menus); }
};

} // namespace

TEST_CASE("MenuButton opens its popup", "[ui_widgets][menu]") {
    MenuWorld test;
    test.world.resource<Events<ui_widgets::Activate>>().send({test.button});

    test.update();

    CHECK(
        test.world.get_component<ui::Node>(test.popup).display ==
        ui::Display::Flex
    );
    CHECK(test.world.has_component<ui_widgets::MenuOpen>(test.button));
    CHECK(
        test.world.resource<input_focus::InputFocus>().get() ==
        Optional<Entity> {test.items[0]}
    );
    const auto event =
        test.world.resource<Events<ui_widgets::MenuEvent>>().get_event(0);
    REQUIRE(event);
    CHECK(event->event.action == ui_widgets::MenuAction::Opened);
}

TEST_CASE("Menu arrow navigation wraps", "[ui_widgets][menu]") {
    MenuWorld test(true);
    test.world.resource<input_focus::InputFocus>().set(test.items[1]);
    test.world.resource<KeyInput>().press(KeyCode::Down);

    test.update();

    CHECK(
        test.world.resource<input_focus::InputFocus>().get() ==
        Optional<Entity> {test.items[0]}
    );
}

TEST_CASE("MenuItem activation closes the popup", "[ui_widgets][menu]") {
    MenuWorld test(true);
    test.world.resource<Events<ui_widgets::Activate>>().send({test.items[1]});

    test.update();

    CHECK(
        test.world.get_component<ui::Node>(test.popup).display ==
        ui::Display::None
    );
    const auto event =
        test.world.resource<Events<ui_widgets::MenuEvent>>().get_event(0);
    REQUIRE(event);
    CHECK(event->event.action == ui_widgets::MenuAction::Activated);
    CHECK(event->event.source == test.items[1]);
}
