#include "input_focus/focus.hpp"

#include "ecs/world.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace ets;

TEST_CASE(
    "Input focus emits coalesced gained and lost events",
    "[input_focus][events]"
) {
    World world;
    world.add_resource(input_focus::InputFocus {});
    world.add_resource(Events<input_focus::FocusGained> {});
    world.add_resource(Events<input_focus::FocusLost> {});
    const auto first = world.entity();
    const auto skipped = world.entity();
    const auto final = world.entity();

    world.resource<input_focus::InputFocus>().set(
        first,
        input_focus::FocusCause::Navigation
    );
    world.run_system_once(input_focus::process_focus_changes);

    const auto first_gained =
        world.resource<Events<input_focus::FocusGained>>().get_event(0);
    REQUIRE(first_gained);
    CHECK(first_gained->event.entity == first);
    CHECK(first_gained->event.cause == input_focus::FocusCause::Navigation);
    CHECK(world.resource<Events<input_focus::FocusLost>>().size() == 0);

    auto& focus = world.resource<input_focus::InputFocus>();
    focus.set(skipped, input_focus::FocusCause::Pointer);
    focus.set(final, input_focus::FocusCause::Programmatic);
    world.run_system_once(input_focus::process_focus_changes);

    const auto lost =
        world.resource<Events<input_focus::FocusLost>>().get_event(0);
    const auto final_gained =
        world.resource<Events<input_focus::FocusGained>>().get_event(1);
    REQUIRE(lost);
    REQUIRE(final_gained);
    CHECK(lost->event.entity == first);
    CHECK(final_gained->event.entity == final);
    CHECK(final_gained->event.cause == input_focus::FocusCause::Programmatic);
    CHECK(world.resource<Events<input_focus::FocusGained>>().size() == 2);
    CHECK(world.resource<Events<input_focus::FocusLost>>().size() == 1);
}

TEST_CASE("AutoFocus acquires programmatic focus", "[input_focus][auto]") {
    World world;
    world.add_resource(input_focus::InputFocus {});
    const auto entity = world.entity();
    world.add_component(entity, input_focus::AutoFocus {});

    world.run_system_once(input_focus::apply_auto_focus);

    const auto& focus = world.resource<input_focus::InputFocus>();
    REQUIRE(focus.get());
    CHECK(*focus.get() == entity);
    CHECK(focus.cause == input_focus::FocusCause::Programmatic);
}

TEST_CASE("Invalid focused entity is cleared", "[input_focus][lifecycle]") {
    World world;
    world.add_resource(input_focus::InputFocus {});
    const auto entity = world.entity();
    world.resource<input_focus::InputFocus>().set(entity);
    world.despawn(entity);

    world.run_system_once(input_focus::clear_invalid_focus);

    CHECK_FALSE(world.resource<input_focus::InputFocus>().get());
}

TEST_CASE("Focus helpers inspect hierarchy and visibility", "[input_focus]") {
    World world;
    const auto root = world.entity();
    const auto child = world.entity();
    const auto leaf = world.entity();
    world.set_parent(child, root);
    world.set_parent(leaf, child);
    input_focus::InputFocus focus;
    focus.set(leaf);
    const input_focus::InputFocusVisible hidden;
    const input_focus::InputFocusVisible visible {.visible = true};

    world.run_system_once([&](Query<Entity, const ChildOf> parents) {
        CHECK(input_focus::is_focused(focus, leaf));
        CHECK_FALSE(input_focus::is_focused(focus, child));
        CHECK(input_focus::is_focus_within(focus, root, parents));
        CHECK(input_focus::is_focus_within(focus, child, parents));
        CHECK_FALSE(input_focus::is_focus_visible(focus, hidden, leaf));
        CHECK(input_focus::is_focus_visible(focus, visible, leaf));
        CHECK(
            input_focus::is_focus_within_visible(focus, visible, root, parents)
        );
    });
}
