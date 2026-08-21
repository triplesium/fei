#include "ui_widgets/select.hpp"

#include "ecs/world.hpp"
#include "input_focus/focus.hpp"
#include "ui/node.hpp"
#include "ui_widgets/list_box.hpp"
#include "ui_widgets/plugin.hpp"
#include "ui_widgets/value_change.hpp"
#include "input/input.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace fei;

namespace {

struct SelectWorld {
    World world;
    Entity select;
    Entity popup;
    Entity list_box;
    Entity item;

    SelectWorld() {
        world.add_resource(MouseInput {});
        world.add_resource(KeyInput {});
        world.add_resource(input_focus::InputFocus {});
        world.add_resource(input_focus::InputFocusVisible {});
        world.add_resource(Events<ui_widgets::Activate> {});
        world.add_resource(Events<ui_widgets::ValueChange<Entity>> {});
        world.add_resource(Events<ui_widgets::SelectionChange> {});
        popup = world.entity();
        world.add_component(popup, ui::Node {.display = ui::Display::None});
        list_box = world.entity();
        world.add_component(list_box, ui_widgets::ListBox {});
        world.add_component(list_box, ui_widgets::ActiveDescendant {});
        world.set_parent(list_box, popup);
        item = world.entity();
        world.add_component(item, ui_widgets::ListItem {});
        world.set_parent(item, list_box);
        select = world.entity();
        world.add_component(
            select,
            ui_widgets::Select {.popup = popup, .list_box = list_box}
        );
    }

    void update() { world.run_system_once(ui_widgets::update_selects); }
};

} // namespace

TEST_CASE("Select activation opens its list popup", "[ui_widgets][select]") {
    SelectWorld test;
    test.world.resource<Events<ui_widgets::Activate>>().send({test.select});

    test.update();

    CHECK(
        test.world.get_component<ui::Node>(test.popup).display ==
        ui::Display::Flex
    );
    CHECK(test.world.has_component<ui_widgets::Expanded>(test.select));
    CHECK(
        test.world.resource<input_focus::InputFocus>().get() ==
        Optional<Entity> {test.list_box}
    );
}

TEST_CASE(
    "Select forwards ListBox selection and closes",
    "[ui_widgets][select]"
) {
    SelectWorld test;
    auto popup_node = test.world.get_component_rw<ui::Node>(test.popup);
    auto open = popup_node.read();
    open.display = ui::Display::Flex;
    popup_node = open;
    test.world.resource<Events<ui_widgets::ValueChange<Entity>>>().send(
        {.source = test.list_box, .value = test.item, .is_final = true}
    );

    test.update();

    const auto event =
        test.world.resource<Events<ui_widgets::SelectionChange>>().get_event(0);
    REQUIRE(event);
    CHECK(event->event.source == test.select);
    CHECK(event->event.option == test.item);
    CHECK(
        test.world.get_component<ui::Node>(test.popup).display ==
        ui::Display::None
    );
}

TEST_CASE("ComboBox opens with the Down key", "[ui_widgets][combo_box]") {
    SelectWorld test;
    test.world.remove_component<ui_widgets::Select>(test.select);
    test.world.add_component(
        test.select,
        ui_widgets::ComboBox {
            .popup = test.popup,
            .list_box = test.list_box,
        }
    );
    test.world.add_component(test.select, ui::Interaction::None);
    test.world.resource<input_focus::InputFocus>().set(test.select);
    test.world.resource<KeyInput>().press(KeyCode::Down);

    test.update();

    CHECK(
        test.world.get_component<ui::Node>(test.popup).display ==
        ui::Display::Flex
    );
    CHECK(
        test.world.resource<input_focus::InputFocus>().get() ==
        Optional<Entity> {test.list_box}
    );
}
