#include "ui_widgets/list_box.hpp"

#include "ecs/world.hpp"
#include "input/input.hpp"
#include "input_focus/focus.hpp"
#include "ui/interaction.hpp"
#include "ui_widgets/plugin.hpp"
#include "ui_widgets/value_change.hpp"

#include <array>
#include <catch2/catch_test_macros.hpp>

using namespace ets;

namespace {

struct ListWorld {
    World world;
    Entity box;
    std::array<Entity, 3> items;

    ListWorld() {
        world.add_resource(KeyInput {});
        world.add_resource(input_focus::InputFocus {});
        world.add_resource(Events<ui_widgets::Activate> {});
        world.add_resource(Events<ui_widgets::SetListSelection> {});
        world.add_resource(Events<ui_widgets::ValueChange<Entity>> {});
        box = world.entity();
        world.add_component(box, ui_widgets::ListBox {});
        world.add_component(box, ui_widgets::ActiveDescendant {});
        for (auto& item : items) {
            item = world.entity();
            world.add_component(item, ui_widgets::ListItem {});
            world.add_component(item, ui::Interaction::None);
            world.set_parent(item, box);
        }
        world.resource<input_focus::InputFocus>().set(box);
    }

    void update() { world.run_system_once(ui_widgets::update_list_boxes); }
};

} // namespace

TEST_CASE(
    "ListBox arrow keys move ActiveDescendant",
    "[ui_widgets][list_box]"
) {
    ListWorld test;
    test.world.resource<KeyInput>().press(KeyCode::Down);

    test.update();

    CHECK(
        test.world.get_component<ui_widgets::ActiveDescendant>(test.box)
            .entity == Optional<Entity> {test.items[0]}
    );
}

TEST_CASE("ListBox activation emits a selection", "[ui_widgets][list_box]") {
    ListWorld test;
    test.world.resource<Events<ui_widgets::Activate>>().send({test.items[1]});

    test.update();

    const auto event =
        test.world.resource<Events<ui_widgets::ValueChange<Entity>>>()
            .get_event(0);
    REQUIRE(event);
    CHECK(event->event.source == test.box);
    CHECK(event->event.value == test.items[1]);
}

TEST_CASE(
    "list_box_self_update maintains one selected item",
    "[ui_widgets][list_box]"
) {
    ListWorld test;
    test.world.add_component(test.items[0], ui::Selected {});
    test.world.resource<Events<ui_widgets::ValueChange<Entity>>>().send(
        {.source = test.box, .value = test.items[2], .is_final = true}
    );

    test.world.run_system_once(ui_widgets::list_box_self_update);

    CHECK_FALSE(test.world.has_component<ui::Selected>(test.items[0]));
    CHECK(test.world.has_component<ui::Selected>(test.items[2]));
}
