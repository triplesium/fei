#include "ui_widgets/scroll_area.hpp"

#include "app/app.hpp"
#include "ecs/world.hpp"
#include "ui/plugin.hpp"
#include "ui_widgets/plugin.hpp"
#include "input/input.hpp"
#include "window/window.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace fei;

namespace {

void add_scroll_area(
    World& world,
    Entity entity,
    float position = 0.0f,
    bool hovered = true
) {
    world.add_component(entity, ui_widgets::ScrollArea {});
    world.add_component(
        entity,
        ui::Node {.overflow = ui::Overflow::scroll_y()}
    );
    world.add_component(
        entity,
        ui::ScrollPosition {.offset = {0.0f, position}}
    );
    world.add_component(
        entity,
        ui::ComputedNode {
            .size = {100.0f, 100.0f},
            .content_size = {100.0f, 100.0f},
            .scroll_content_size = {100.0f, 200.0f},
        }
    );
    world.add_component(
        entity,
        ui::RelativeCursorPosition {.cursor_over = hovered}
    );
}

struct ScrollWorld {
    World world;

    ScrollWorld() {
        world.add_resource(MouseScrollInput {});
        world.add_resource(Events<ui_widgets::ScrollIntoView> {});
    }

    void update() { world.run_system_once(ui_widgets::update_scroll_areas); }
};

} // namespace

TEST_CASE("ScrollArea consumes vertical wheel input", "[ui_widgets][scroll]") {
    ScrollWorld test;
    const auto area = test.world.entity();
    add_scroll_area(test.world, area);
    test.world.resource<MouseScrollInput>().scroll({0.0f, -1.0f});

    test.update();

    CHECK(
        test.world.get_component<ui::ScrollPosition>(area).offset.y ==
        Catch::Approx(20.0f)
    );
}

TEST_CASE(
    "Nested ScrollArea passes unused wheel delta to its parent",
    "[ui_widgets][scroll]"
) {
    ScrollWorld test;
    const auto outer = test.world.entity();
    const auto inner = test.world.entity();
    add_scroll_area(test.world, outer);
    add_scroll_area(test.world, inner, 100.0f);
    test.world.set_parent(inner, outer);
    test.world.resource<MouseScrollInput>().scroll({0.0f, -1.0f});

    test.update();

    CHECK(
        test.world.get_component<ui::ScrollPosition>(inner).offset.y ==
        Catch::Approx(100.0f)
    );
    CHECK(
        test.world.get_component<ui::ScrollPosition>(outer).offset.y ==
        Catch::Approx(20.0f)
    );
}

TEST_CASE(
    "ScrollIntoView uses the nearest ScrollArea",
    "[ui_widgets][scroll]"
) {
    ScrollWorld test;
    const auto area = test.world.entity();
    const auto target = test.world.entity();
    add_scroll_area(test.world, area, 0.0f, false);
    test.world.add_component(
        target,
        ui::ComputedNode {
            .position = {0.0f, 150.0f},
            .size = {100.0f, 20.0f},
        }
    );
    test.world.set_parent(target, area);
    test.world.resource<Events<ui_widgets::ScrollIntoView>>().send({target});

    test.update();

    CHECK(
        test.world.get_component<ui::ScrollPosition>(area).offset.y ==
        Catch::Approx(70.0f)
    );
}

TEST_CASE(
    "ScrollAreaPlugin inserts required components",
    "[ui_widgets][scroll]"
) {
    App app;
    app.add_resource(
        Window {.glfw_window = nullptr, .width = 200, .height = 120}
    );
    app.add_plugin<ui_widgets::ScrollAreaPlugin>();
    app.finish();

    const auto entity = app.world().entity();
    app.world().add_component(entity, ui_widgets::ScrollArea {});
    app.world().sort_systems();
    app.run_schedule(PostUpdate);

    CHECK(app.world().has_component<ui::Node>(entity));
    CHECK(app.world().has_component<ui::ScrollPosition>(entity));
    CHECK(app.world().has_component<ui::RelativeCursorPosition>(entity));
}
