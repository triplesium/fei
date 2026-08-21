#include "app/app.hpp"
#include "asset/assets.hpp"
#include "core/image.hpp"
#include "ecs/world.hpp"
#include "graphics/enums.hpp"
#include "ui/image.hpp"
#include "ui/plugin.hpp"
#include "ui/surface.hpp"
#include "ui/text.hpp"
#include "window/window.hpp"

#include <array>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace fei;

namespace {

void check_near(float actual, float expected) {
    CHECK(actual == Catch::Approx(expected).margin(0.0001f));
}

void check_vector(Vector2 actual, float x, float y) {
    check_near(actual.x, x);
    check_near(actual.y, y);
}

constexpr Entity test_entity(uint32 value) {
    return Entity {value};
}

} // namespace

TEST_CASE("UI root fills its viewport", "[ui][layout]") {
    ui::Surface surface;
    surface.upsert(test_entity(1), ui::Node {});

    surface.compute(test_entity(1), {1280.0f, 720.0f});

    const auto* root = surface.get(test_entity(1));
    REQUIRE(root);
    check_vector(root->position, 0.0f, 0.0f);
    check_vector(root->size, 1280.0f, 720.0f);
    check_vector(root->content_size, 1280.0f, 720.0f);
}

TEST_CASE("UI border participates in the border-box layout", "[ui][layout]") {
    ui::Surface surface;
    surface.upsert(
        test_entity(1),
        ui::Node {
            .border = ui::all(ui::px(4.0f)),
            .padding = ui::all(ui::px(6.0f)),
        },
        Vector2::Zero,
        ui::BorderRadius::all(ui::px(20.0f))
    );
    surface.upsert(test_entity(2), ui::Node {.flex_grow = 1.0f});
    const std::array children {Entity {2}};
    surface.set_children(test_entity(1), children);

    surface.compute(test_entity(1), {100.0f, 60.0f});

    const auto* root = surface.get(test_entity(1));
    const auto* child = surface.get(test_entity(2));
    REQUIRE(root);
    REQUIRE(child);
    check_vector(root->content_size, 80.0f, 40.0f);
    CHECK(root->border.left == 4.0f);
    CHECK(root->border_radius.top_left == 20.0f);
    check_vector(child->position, 10.0f, 10.0f);
    check_vector(child->size, 80.0f, 40.0f);
}

TEST_CASE("UI column lays out fixed children with a gap", "[ui][layout]") {
    ui::Surface surface;
    surface.upsert(
        test_entity(1),
        ui::Node {
            .padding = ui::all(ui::px(10.0f)),
            .gap = ui::px(5.0f),
        }
    );
    surface.upsert(test_entity(2), ui::Node {.height = ui::px(20.0f)});
    surface.upsert(test_entity(3), ui::Node {.height = ui::px(30.0f)});
    const std::array children {Entity {2}, Entity {3}};
    surface.set_children(test_entity(1), children);

    surface.compute(test_entity(1), {200.0f, 100.0f});

    const auto* first = surface.get(test_entity(2));
    const auto* second = surface.get(test_entity(3));
    REQUIRE(first);
    REQUIRE(second);
    check_vector(first->position, 10.0f, 10.0f);
    check_vector(first->size, 180.0f, 20.0f);
    check_vector(second->position, 10.0f, 35.0f);
    check_vector(second->size, 180.0f, 30.0f);
}

TEST_CASE("UI scroll position offsets and clamps content", "[ui][layout]") {
    ui::Surface surface;
    surface.upsert(
        test_entity(1),
        ui::Node {.overflow = ui::Overflow::scroll_y()},
        ui::ContentSize {},
        ui::BorderRadius {},
        ui::ScrollPosition {.offset = {0.0f, 500.0f}}
    );
    surface.upsert(
        test_entity(2),
        ui::Node {
            .height = ui::px(300.0f),
            .flex_shrink = 0.0f,
        }
    );
    const std::array children {Entity {2}};
    surface.set_children(test_entity(1), children);

    surface.compute(test_entity(1), {100.0f, 100.0f});

    const auto* area = surface.get(test_entity(1));
    const auto* content = surface.get(test_entity(2));
    REQUIRE(area);
    REQUIRE(content);
    check_vector(area->scroll_content_size, 100.0f, 300.0f);
    check_vector(area->scroll_position, 0.0f, 200.0f);
    check_vector(content->position, 0.0f, -200.0f);
}

TEST_CASE("UI row distributes remaining space by flex grow", "[ui][layout]") {
    ui::Surface surface;
    surface.upsert(
        test_entity(1),
        ui::Node {.flex_direction = ui::FlexDirection::Row}
    );
    surface.upsert(test_entity(2), ui::Node {.flex_grow = 1.0f});
    surface.upsert(test_entity(3), ui::Node {.flex_grow = 3.0f});
    const std::array children {Entity {2}, Entity {3}};
    surface.set_children(test_entity(1), children);

    surface.compute(test_entity(1), {400.0f, 100.0f});

    const auto* first = surface.get(test_entity(2));
    const auto* second = surface.get(test_entity(3));
    REQUIRE(first);
    REQUIRE(second);
    check_vector(first->size, 100.0f, 100.0f);
    check_vector(second->position, 100.0f, 0.0f);
    check_vector(second->size, 300.0f, 100.0f);
}

TEST_CASE(
    "UI percentages resolve inside the parent content box",
    "[ui][layout]"
) {
    ui::Surface surface;
    surface.upsert(
        test_entity(1),
        ui::Node {
            .padding = ui::axes(ui::px(20.0f), ui::px(10.0f)),
            .flex_direction = ui::FlexDirection::Row,
        }
    );
    surface.upsert(test_entity(2), ui::Node {.width = ui::percent(25.0f)});
    surface.upsert(test_entity(3), ui::Node {.flex_grow = 1.0f});
    const std::array children {Entity {2}, Entity {3}};
    surface.set_children(test_entity(1), children);

    surface.compute(test_entity(1), {440.0f, 120.0f});

    const auto* sidebar = surface.get(test_entity(2));
    const auto* content = surface.get(test_entity(3));
    REQUIRE(sidebar);
    REQUIRE(content);
    check_vector(sidebar->position, 20.0f, 10.0f);
    check_vector(sidebar->size, 100.0f, 100.0f);
    check_vector(content->position, 120.0f, 10.0f);
    check_vector(content->size, 300.0f, 100.0f);
}

TEST_CASE("UI absolute nodes do not participate in flex flow", "[ui][layout]") {
    ui::Surface surface;
    surface.upsert(test_entity(1), ui::Node {});
    surface.upsert(test_entity(2), ui::Node {.height = ui::px(40.0f)});
    surface.upsert(
        test_entity(3),
        ui::Node {
            .position_type = ui::PositionType::Absolute,
            .width = ui::px(50.0f),
            .height = ui::px(20.0f),
            .right = ui::px(10.0f),
            .bottom = ui::px(5.0f),
        }
    );
    const std::array children {Entity {2}, Entity {3}};
    surface.set_children(test_entity(1), children);

    surface.compute(test_entity(1), {200.0f, 100.0f});

    const auto* flow = surface.get(test_entity(2));
    const auto* absolute = surface.get(test_entity(3));
    REQUIRE(flow);
    REQUIRE(absolute);
    check_vector(flow->position, 0.0f, 0.0f);
    check_vector(flow->size, 200.0f, 40.0f);
    check_vector(absolute->position, 140.0f, 75.0f);
    check_vector(absolute->size, 50.0f, 20.0f);
}

TEST_CASE(
    "UI content size supplies intrinsic layout dimensions",
    "[ui][layout]"
) {
    ui::Surface surface;
    surface.upsert(
        test_entity(1),
        ui::Node {
            .flex_direction = ui::FlexDirection::Row,
            .align_items = ui::AlignItems::Start,
        }
    );
    surface.upsert(
        test_entity(2),
        ui::Node {.width = ui::px(100.0f)},
        Vector2 {200.0f, 100.0f}
    );
    const std::array children {Entity {2}};
    surface.set_children(test_entity(1), children);

    surface.compute(test_entity(1), {300.0f, 200.0f});

    const auto* image = surface.get(test_entity(2));
    REQUIRE(image);
    check_vector(image->size, 100.0f, 50.0f);
}

TEST_CASE(
    "UI plugin derives computed nodes from ECS hierarchy",
    "[ui][layout]"
) {
    App app;
    app.add_resource(Window {.width = 320, .height = 180});
    app.add_plugin<ui::UiPlugin>();
    app.finish();

    const auto root = app.world().entity();
    const auto child = app.world().entity();
    app.world().add_component(
        root,
        ui::Node {.flex_direction = ui::FlexDirection::Row}
    );
    app.world().add_component(child, ui::Node {.width = ui::percent(50.0f)});
    app.world().set_parent(child, root);

    app.world().sort_systems();
    app.run_schedule(PostUpdate);

    REQUIRE(app.world().has_component<ui::ComputedNode>(root));
    REQUIRE(app.world().has_component<ui::ComputedNode>(child));
    check_vector(
        app.world().get_component<ui::ComputedNode>(root).size,
        320.0f,
        180.0f
    );
    check_vector(
        app.world().get_component<ui::ComputedNode>(child).size,
        160.0f,
        180.0f
    );
}

TEST_CASE("UI image assets update intrinsic content size", "[ui][image]") {
    App app;
    app.add_resource(Window {.width = 320, .height = 180});
    app.add_plugin<ui::UiPlugin>();
    app.finish();

    auto source = Image::create_empty(
        64,
        32,
        1,
        PixelFormat::Rgba8Unorm,
        TextureUsage::Sampled,
        TextureType::Texture2D
    );
    const auto image = app.resource<Assets<Image>>().add(std::move(source));
    const auto entity = app.world().entity();
    app.world().add_component(entity, ui::ImageNode {.image = image});

    app.world().sort_systems();
    app.run_schedule(PostUpdate);

    REQUIRE(app.world().has_component<ui::ContentSize>(entity));
    CHECK(
        app.world().get_component<ui::ContentSize>(entity).compute(
            ui::MeasureArgs {
                .available_width = ui::AvailableSpace::max_content(),
                .available_height = ui::AvailableSpace::max_content(),
            }
        ) == Vector2 {64.0f, 32.0f}
    );
}

TEST_CASE("UI text inserts its required layout components", "[ui][text]") {
    App app;
    app.add_resource(Window {.width = 320, .height = 180});
    app.add_plugin<ui::UiPlugin>();
    app.finish();

    const auto entity = app.world().entity();
    app.world().add_component(entity, ui::Text {.value = "Hello"});

    app.world().sort_systems();
    app.run_schedule(PostUpdate);

    CHECK(app.world().has_component<ui::Node>(entity));
    CHECK(app.world().has_component<ui::ContentSize>(entity));
    CHECK(app.world().has_component<text::TextFont>(entity));
    CHECK(app.world().has_component<text::TextColor>(entity));
    CHECK(app.world().has_component<text::TextLayout>(entity));
    CHECK(app.world().has_component<text::TextLayoutInfo>(entity));
    CHECK(app.world().has_component<ui::TextNodeFlags>(entity));
}

TEST_CASE("UI text and layout stay cached until inputs change", "[ui][text]") {
    App app;
    app.add_resource(Window {.width = 320, .height = 180});
    app.add_plugin<ui::UiPlugin>();
    app.finish();

    const auto entity = app.world().entity();
    app.world().add_component(entity, ui::Text {.value = "Cached"});

    app.world().sort_systems();
    app.run_schedule(PostUpdate);
    app.run_schedule(PostUpdate);

    const auto content_tick =
        app.world().get_component_rw<ui::ContentSize>(entity).changed_tick();
    const auto text_layout_tick =
        app.world()
            .get_component_rw<text::TextLayoutInfo>(entity)
            .changed_tick();
    const auto generation = app.resource<ui::LayoutState>().generation;
    const auto& initial_flags =
        app.world().get_component<ui::TextNodeFlags>(entity);
    CHECK_FALSE(initial_flags.needs_measure);
    CHECK_FALSE(initial_flags.needs_layout);

    app.run_schedule(PostUpdate);

    CHECK(
        app.world().get_component_rw<ui::ContentSize>(entity).changed_tick() ==
        content_tick
    );
    CHECK(
        app.world()
            .get_component_rw<text::TextLayoutInfo>(entity)
            .changed_tick() == text_layout_tick
    );
    CHECK(app.resource<ui::LayoutState>().generation == generation);

    app.world().get_component_rw<text::TextLayout>(entity)->justify =
        text::Justify::Center;
    app.run_schedule(PostUpdate);

    CHECK(
        app.world().get_component_rw<ui::ContentSize>(entity).changed_tick() >
        content_tick
    );
    CHECK(app.resource<ui::LayoutState>().generation == generation + 1);
    const auto& updated_flags =
        app.world().get_component<ui::TextNodeFlags>(entity);
    CHECK_FALSE(updated_flags.needs_measure);
    CHECK_FALSE(updated_flags.needs_layout);
}

TEST_CASE("UI text measure changes Flex sibling placement", "[ui][text]") {
    ui::Surface surface;
    surface.upsert(test_entity(1), ui::Node {});
    const text::TextMeasureInfo info {
        .min = {10.0f, 10.0f},
        .max = {25.0f, 10.0f},
        .ascent = 8.0f,
        .line_height = 10.0f,
        .glyphs = {
            {.codepoint = U'a', .advance = 5.0f},
            {.codepoint = U'a', .advance = 5.0f},
            {.codepoint = U' ', .advance = 5.0f, .whitespace = true},
            {.codepoint = U'a', .advance = 5.0f},
            {.codepoint = U'a', .advance = 5.0f},
        },
    };
    surface.upsert(
        test_entity(2),
        ui::Node {},
        ui::ContentSize {
            .measure = ui::TextMeasure {
                .info = info,
                .layout = text::TextLayout {},
            },
        }
    );
    surface.upsert(
        test_entity(3),
        ui::Node {.height = ui::px(10.0f)},
        Vector2::Zero
    );
    const std::array children {Entity {2}, Entity {3}};
    surface.set_children(test_entity(1), children);

    surface.compute(test_entity(1), {15.0f, 100.0f});

    const auto* text_node = surface.get(test_entity(2));
    const auto* sibling = surface.get(test_entity(3));
    REQUIRE(text_node);
    REQUIRE(sibling);
    check_vector(text_node->size, 15.0f, 20.0f);
    check_vector(sibling->position, 0.0f, 20.0f);
}

TEST_CASE("UI stack orders parents before z-sorted siblings", "[ui][stack]") {
    App app;
    app.add_resource(Window {.width = 320, .height = 180});
    app.add_plugin<ui::UiPlugin>();
    app.finish();

    const auto root = app.world().entity();
    const auto front = app.world().entity();
    const auto back = app.world().entity();
    const auto nested = app.world().entity();
    app.world().add_component(root, ui::Node {});
    app.world().add_component(front, ui::Node {});
    app.world().add_component(front, ui::ZIndex {.value = 2});
    app.world().add_component(back, ui::Node {});
    app.world().add_component(back, ui::ZIndex {.value = -1});
    app.world().add_component(nested, ui::Node {});
    app.world().set_parent(front, root);
    app.world().set_parent(back, root);
    app.world().set_parent(nested, back);

    app.world().sort_systems();
    app.run_schedule(PostUpdate);

    const auto& stack = app.resource<ui::Stack>().nodes;
    REQUIRE(stack == std::vector<Entity> {root, back, nested, front});
    CHECK(app.world().get_component<ui::ComputedStackIndex>(root).value == 0);
    CHECK(app.world().get_component<ui::ComputedStackIndex>(back).value == 1);
    CHECK(app.world().get_component<ui::ComputedStackIndex>(nested).value == 2);
    CHECK(app.world().get_component<ui::ComputedStackIndex>(front).value == 3);
}

TEST_CASE("UI stack hides descendants of display-none nodes", "[ui][stack]") {
    App app;
    app.add_resource(Window {.width = 320, .height = 180});
    app.add_plugin<ui::UiPlugin>();
    app.finish();

    const auto root = app.world().entity();
    const auto hidden = app.world().entity();
    const auto child = app.world().entity();
    app.world().add_component(root, ui::Node {});
    app.world().add_component(hidden, ui::Node {.display = ui::Display::None});
    app.world().add_component(child, ui::Node {});
    app.world().set_parent(hidden, root);
    app.world().set_parent(child, hidden);

    app.world().sort_systems();
    app.run_schedule(PostUpdate);

    REQUIRE(app.resource<ui::Stack>().nodes == std::vector<Entity> {root});
    CHECK(
        app.world().get_component<ui::ComputedStackIndex>(hidden).value ==
        ui::ComputedStackIndex::HIDDEN
    );
    CHECK(
        app.world().get_component<ui::ComputedStackIndex>(child).value ==
        ui::ComputedStackIndex::HIDDEN
    );
}

TEST_CASE("UI clipping propagates and intersects by axis", "[ui][clipping]") {
    App app;
    app.add_resource(Window {.width = 200, .height = 100});
    app.add_plugin<ui::UiPlugin>();
    app.finish();

    const auto root = app.world().entity();
    const auto child = app.world().entity();
    const auto grandchild = app.world().entity();
    app.world().add_component(
        root,
        ui::Node {.overflow = ui::Overflow::clip_x()}
    );
    app.world().add_component(
        child,
        ui::Node {
            .position_type = ui::PositionType::Absolute,
            .overflow = ui::Overflow::clip_y(),
            .width = ui::px(100.0f),
            .height = ui::px(60.0f),
            .left = ui::px(150.0f),
            .top = ui::px(20.0f),
        }
    );
    app.world().add_component(grandchild, ui::Node {});
    app.world().set_parent(child, root);
    app.world().set_parent(grandchild, child);

    app.world().sort_systems();
    app.run_schedule(PostUpdate);

    CHECK_FALSE(app.world().has_component<ui::CalculatedClip>(root));
    const auto& child_clip =
        app.world().get_component<ui::CalculatedClip>(child).clip;
    CHECK(child_clip.min.x == 0.0f);
    CHECK(child_clip.max.x == 200.0f);
    CHECK(child_clip.min.y < -1.0e30f);
    CHECK(child_clip.max.y > 1.0e30f);

    const auto& grandchild_clip =
        app.world().get_component<ui::CalculatedClip>(grandchild).clip;
    CHECK(grandchild_clip.min == Vector2 {0.0f, 20.0f});
    CHECK(grandchild_clip.max == Vector2 {200.0f, 80.0f});
}
