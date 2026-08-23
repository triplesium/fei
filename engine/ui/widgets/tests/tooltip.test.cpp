#include "ui_widgets/tooltip.hpp"

#include "core/time.hpp"
#include "ecs/world.hpp"
#include "ui/node.hpp"
#include "ui_widgets/plugin.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace ets;

TEST_CASE("Tooltip appears after its hover delay", "[ui_widgets][tooltip]") {
    World world;
    Time time;
    time.set_fixed_delta(0.25f);
    time.tick();
    world.add_resource(std::move(time));
    const auto content = world.entity();
    world.add_component(content, ui::Node {.display = ui::Display::None});
    const auto anchor = world.entity();
    world.add_component(
        anchor,
        ui_widgets::Tooltip {.content = content, .delay = 0.5f}
    );
    world.add_component(anchor, ui::Interaction::Hovered);
    world.add_component(anchor, ui_widgets::TooltipState {});

    world.run_system_once(ui_widgets::update_tooltips);
    CHECK(world.get_component<ui::Node>(content).display == ui::Display::None);

    world.resource<Time>().tick();
    world.run_system_once(ui_widgets::update_tooltips);
    CHECK(world.get_component<ui::Node>(content).display == ui::Display::Flex);
}

TEST_CASE("Tooltip hides when hover ends", "[ui_widgets][tooltip]") {
    World world;
    Time time;
    time.set_fixed_delta(0.1f);
    time.tick();
    world.add_resource(std::move(time));
    const auto content = world.entity();
    world.add_component(content, ui::Node {});
    const auto anchor = world.entity();
    world.add_component(
        anchor,
        ui_widgets::Tooltip {.content = content, .delay = 0.0f}
    );
    world.add_component(anchor, ui::Interaction::None);
    world.add_component(
        anchor,
        ui_widgets::TooltipState {.hovered_time = 1.0f, .visible = true}
    );

    world.run_system_once(ui_widgets::update_tooltips);

    CHECK(world.get_component<ui::Node>(content).display == ui::Display::None);
    CHECK_FALSE(world.get_component<ui_widgets::TooltipState>(anchor).visible);
}
