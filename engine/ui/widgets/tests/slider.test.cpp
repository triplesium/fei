#include "ui_widgets/slider.hpp"

#include "app/app.hpp"
#include "ecs/world.hpp"
#include "input_focus/focus.hpp"
#include "input_focus/tab_navigation.hpp"
#include "ui/plugin.hpp"
#include "ui_widgets/plugin.hpp"
#include "ui_widgets/value_change.hpp"
#include "window/input.hpp"
#include "window/window.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace fei;

namespace {

struct SliderWorld {
    World world;
    Entity slider;
    Entity thumb;

    SliderWorld(
        ui_widgets::SliderValue value = {.value = 0.5f},
        ui_widgets::SliderRange range =
            ui_widgets::SliderRange::new_range(0.0f, 1.0f),
        ui_widgets::SliderStep step = {.value = 0.1f},
        ui_widgets::Slider slider_component = {}
    ) {
        world.add_resource(
            Window {.glfw_window = nullptr, .width = 200, .height = 120}
        );
        world.add_resource(KeyInput {});
        world.add_resource(MouseInput {});
        world.add_resource(Events<ui_widgets::SetSliderValue> {});
        world.add_resource(Events<ui_widgets::ValueChange<float>> {});
        world.add_resource(input_focus::InputFocus {});
        world.add_resource(input_focus::InputFocusVisible {});

        slider = world.entity();
        world.add_component(slider, slider_component);
        world.add_component(slider, value);
        world.add_component(slider, range);
        world.add_component(slider, step);
        world.add_component(slider, ui::Node {});
        world.add_component(slider, ui::Interaction::None);
        world.add_component(slider, ui::FocusPolicy::Block);
        world.add_component(slider, ui_widgets::SliderDragState {});
        world.add_component(
            slider,
            ui::ComputedNode {
                .position = {10.0f, 10.0f},
                .size = {100.0f, 20.0f},
            }
        );

        thumb = world.entity();
        world.add_component(thumb, ui_widgets::SliderThumb {});
        world.add_component(thumb, ui::Node {});
        world.add_component(thumb, ui::FocusPolicy::Pass);
        world.add_component(
            thumb,
            ui::ComputedNode {
                .position = {50.0f, 10.0f},
                .size = {20.0f, 20.0f},
            }
        );
        world.set_parent(thumb, slider);
        world.add_resource(ui::Stack {.nodes = {slider, thumb}});
    }

    void update() { world.run_system_once(ui_widgets::update_sliders); }

    KeyInput& keyboard() { return world.resource<KeyInput>(); }
    MouseInput& mouse() { return world.resource<MouseInput>(); }

    void press(Vector2 position) {
        mouse().set_position(position);
        mouse().press(MouseButton::Left);
        world.add_component(slider, ui::Interaction::Pressed);
        update();
    }

    void move(Vector2 position) {
        mouse().clear();
        mouse().press(MouseButton::Left);
        mouse().set_position(position);
        update();
    }

    void release(Vector2 position) {
        mouse().clear();
        mouse().set_position(position);
        world.add_component(slider, ui::Interaction::None);
        update();
    }

    const Events<ui_widgets::ValueChange<float>>& changes() const {
        return world.resource<Events<ui_widgets::ValueChange<float>>>();
    }
};

} // namespace

TEST_CASE(
    "SliderRange provides Bevy-style range helpers",
    "[ui_widgets][slider]"
) {
    const auto range = ui_widgets::SliderRange::new_range(-2.0f, 6.0f);

    CHECK(range.start() == Catch::Approx(-2.0f));
    CHECK(range.end() == Catch::Approx(6.0f));
    CHECK(range.span() == Catch::Approx(8.0f));
    CHECK(range.center() == Catch::Approx(2.0f));
    CHECK(range.clamp(-4.0f) == Catch::Approx(-2.0f));
    CHECK(range.clamp(8.0f) == Catch::Approx(6.0f));
    CHECK(range.thumb_position(2.0f) == Catch::Approx(0.5f));
}

TEST_CASE("SliderPrecision rounds decimal places", "[ui_widgets][slider]") {
    CHECK(
        ui_widgets::SliderPrecision {.decimal_places = 2}.round(1.234f) ==
        Catch::Approx(1.23f)
    );
    CHECK(
        ui_widgets::SliderPrecision {.decimal_places = 0}.round(1.6f) ==
        Catch::Approx(2.0f)
    );
    CHECK(
        ui_widgets::SliderPrecision {.decimal_places = -1}.round(16.0f) ==
        Catch::Approx(20.0f)
    );
}

TEST_CASE("SetSliderValue clamps absolute values", "[ui_widgets][slider]") {
    SliderWorld test;
    test.world.resource<Events<ui_widgets::SetSliderValue>>().send({
        .entity = test.slider,
        .change = ui_widgets::SliderValueChange::absolute(2.0f),
    });

    test.update();

    const auto change = test.changes().get_event(0);
    REQUIRE(change);
    CHECK(change->event.source == test.slider);
    CHECK(change->event.value == Catch::Approx(1.0f));
    CHECK(change->event.is_final);
    CHECK(
        test.world.get_component<ui_widgets::SliderValue>(test.slider).value ==
        Catch::Approx(0.5f)
    );
}

TEST_CASE("SetSliderValue supports relative changes", "[ui_widgets][slider]") {
    SliderWorld test;
    test.world.resource<Events<ui_widgets::SetSliderValue>>().send({
        .entity = test.slider,
        .change = ui_widgets::SliderValueChange::relative(-0.2f),
    });

    test.update();

    const auto change = test.changes().get_event(0);
    REQUIRE(change);
    CHECK(change->event.value == Catch::Approx(0.3f));
}

TEST_CASE(
    "SetSliderValue supports relative step changes",
    "[ui_widgets][slider]"
) {
    SliderWorld test;
    test.world.resource<Events<ui_widgets::SetSliderValue>>().send({
        .entity = test.slider,
        .change = ui_widgets::SliderValueChange::relative_step(2.0f),
    });

    test.update();

    const auto change = test.changes().get_event(0);
    REQUIRE(change);
    CHECK(change->event.value == Catch::Approx(0.7f));
}

TEST_CASE(
    "SetSliderValue controls disabled sliders programmatically",
    "[ui_widgets][slider]"
) {
    SliderWorld test;
    test.world.add_component(test.slider, ui::InteractionDisabled {});
    test.world.resource<Events<ui_widgets::SetSliderValue>>().send({
        .entity = test.slider,
        .change = ui_widgets::SliderValueChange::absolute(0.8f),
    });

    test.update();

    const auto change = test.changes().get_event(0);
    REQUIRE(change);
    CHECK(change->event.value == Catch::Approx(0.8f));
}

TEST_CASE(
    "Horizontal Slider uses arrow step navigation",
    "[ui_widgets][slider]"
) {
    SliderWorld test;
    test.world.resource<input_focus::InputFocus>().set(test.slider);
    test.keyboard().press(KeyCode::Right);

    test.update();

    const auto change = test.changes().get_event(0);
    REQUIRE(change);
    CHECK(change->event.value == Catch::Approx(0.6f));
}

TEST_CASE(
    "Vertical Slider uses up and down navigation",
    "[ui_widgets][slider]"
) {
    SliderWorld test(
        {.value = 0.5f},
        ui_widgets::SliderRange::new_range(0.0f, 1.0f),
        {.value = 0.25f},
        {.orientation = ui_widgets::SliderOrientation::Vertical}
    );
    test.world.resource<input_focus::InputFocus>().set(test.slider);
    test.keyboard().press(KeyCode::Up);

    test.update();

    const auto change = test.changes().get_event(0);
    REQUIRE(change);
    CHECK(change->event.value == Catch::Approx(0.75f));
}

TEST_CASE("Auto Slider detects vertical dimensions", "[ui_widgets][slider]") {
    SliderWorld test;
    test.world.add_component(
        test.slider,
        ui::ComputedNode {
            .size = {20.0f, 100.0f},
        }
    );
    test.world.resource<input_focus::InputFocus>().set(test.slider);
    test.keyboard().press(KeyCode::Down);

    test.update();

    const auto change = test.changes().get_event(0);
    REQUIRE(change);
    CHECK(change->event.value == Catch::Approx(0.4f));
}

TEST_CASE("Slider Home and End select range bounds", "[ui_widgets][slider]") {
    SliderWorld home;
    home.world.resource<input_focus::InputFocus>().set(home.slider);
    home.keyboard().press(KeyCode::Home);
    home.update();
    REQUIRE(home.changes().get_event(0));
    CHECK(home.changes().get_event(0)->event.value == Catch::Approx(0.0f));

    SliderWorld end;
    end.world.resource<input_focus::InputFocus>().set(end.slider);
    end.keyboard().press(KeyCode::End);
    end.update();
    REQUIRE(end.changes().get_event(0));
    CHECK(end.changes().get_event(0)->event.value == Catch::Approx(1.0f));
}

TEST_CASE("Disabled Slider ignores keyboard input", "[ui_widgets][slider]") {
    SliderWorld test;
    test.world.add_component(test.slider, ui::InteractionDisabled {});
    test.world.resource<input_focus::InputFocus>().set(test.slider);
    test.keyboard().press(KeyCode::Right);

    test.update();

    CHECK(test.changes().size() == 0);
}

TEST_CASE(
    "Slider drags relative to its starting value",
    "[ui_widgets][slider]"
) {
    SliderWorld test;
    test.press({60.0f, 20.0f});
    CHECK(test.changes().size() == 0);

    test.move({80.0f, 20.0f});
    REQUIRE(test.changes().get_event(0));
    CHECK(test.changes().get_event(0)->event.value == Catch::Approx(0.75f));
    CHECK_FALSE(test.changes().get_event(0)->event.is_final);

    test.release({90.0f, 20.0f});
    REQUIRE(test.changes().get_event(1));
    CHECK(test.changes().get_event(1)->event.value == Catch::Approx(0.875f));
    CHECK(test.changes().get_event(1)->event.is_final);
    CHECK_FALSE(test.world
                    .get_component<ui_widgets::SliderDragState>(test.slider)
                    .dragging);
}

TEST_CASE(
    "Slider captures thumb dragging outside its bounds",
    "[ui_widgets][slider]"
) {
    SliderWorld test;
    test.mouse().set_position({60.0f, 20.0f});
    test.mouse().press(MouseButton::Left);
    test.world.run_system_once(ui::update_interactions);
    CHECK(
        test.world.get_component<ui::Interaction>(test.slider) ==
        ui::Interaction::Pressed
    );
    test.update();

    test.mouse().clear();
    test.mouse().press(MouseButton::Left);
    test.mouse().set_position({180.0f, 100.0f});
    test.world.run_system_once(ui::update_interactions);
    CHECK(
        test.world.get_component<ui::Interaction>(test.slider) ==
        ui::Interaction::Pressed
    );
    test.update();

    REQUIRE(test.changes().get_event(0));
    CHECK(test.changes().get_event(0)->event.value == Catch::Approx(1.0f));
    CHECK_FALSE(test.changes().get_event(0)->event.is_final);

    test.mouse().clear();
    test.world.run_system_once(ui::update_interactions);
    test.update();

    REQUIRE(test.changes().get_event(1));
    CHECK(test.changes().get_event(1)->event.value == Catch::Approx(1.0f));
    CHECK(test.changes().get_event(1)->event.is_final);
}

TEST_CASE("Drag track clicks do not snap the value", "[ui_widgets][slider]") {
    SliderWorld test;
    test.press({90.0f, 20.0f});
    CHECK(test.changes().size() == 0);

    test.release({90.0f, 20.0f});
    CHECK(test.changes().size() == 0);
}

TEST_CASE(
    "Snap track clicks use thumb travel and precision",
    "[ui_widgets][slider]"
) {
    SliderWorld test(
        {.value = 0.5f},
        ui_widgets::SliderRange::new_range(0.0f, 1.0f),
        {.value = 0.1f},
        {.track_click = ui_widgets::TrackClick::Snap}
    );
    test.world.add_component(
        test.slider,
        ui_widgets::SliderPrecision {.decimal_places = 1}
    );

    test.press({86.4f, 20.0f});
    REQUIRE(test.changes().get_event(0));
    CHECK(test.changes().get_event(0)->event.value == Catch::Approx(0.8f));
    CHECK_FALSE(test.changes().get_event(0)->event.is_final);

    test.release({86.4f, 20.0f});
    REQUIRE(test.changes().get_event(1));
    CHECK(test.changes().get_event(1)->event.value == Catch::Approx(0.8f));
    CHECK(test.changes().get_event(1)->event.is_final);
}

TEST_CASE("Step track clicks move by SliderStep", "[ui_widgets][slider]") {
    SliderWorld test(
        {.value = 0.5f},
        ui_widgets::SliderRange::new_range(0.0f, 1.0f),
        {.value = 0.1f},
        {.track_click = ui_widgets::TrackClick::Step}
    );

    test.press({20.0f, 20.0f});
    REQUIRE(test.changes().get_event(0));
    CHECK(test.changes().get_event(0)->event.value == Catch::Approx(0.4f));
}

TEST_CASE("Vertical Slider dragging increases upward", "[ui_widgets][slider]") {
    SliderWorld test(
        {.value = 0.5f},
        ui_widgets::SliderRange::new_range(0.0f, 1.0f),
        {.value = 0.1f},
        {.orientation = ui_widgets::SliderOrientation::Vertical}
    );
    test.world.add_component(
        test.slider,
        ui::ComputedNode {
            .position = {10.0f, 10.0f},
            .size = {20.0f, 100.0f},
        }
    );
    test.world.add_component(
        test.thumb,
        ui::ComputedNode {
            .position = {10.0f, 50.0f},
            .size = {20.0f, 20.0f},
        }
    );

    test.press({20.0f, 60.0f});
    test.move({20.0f, 40.0f});

    REQUIRE(test.changes().get_event(0));
    CHECK(test.changes().get_event(0)->event.value == Catch::Approx(0.75f));
}

TEST_CASE("Disabled Slider ignores pointer dragging", "[ui_widgets][slider]") {
    SliderWorld test;
    test.world.add_component(test.slider, ui::InteractionDisabled {});

    test.press({60.0f, 20.0f});
    test.move({80.0f, 20.0f});
    test.release({80.0f, 20.0f});

    CHECK(test.changes().size() == 0);
}

TEST_CASE(
    "slider_self_update applies the last value change",
    "[ui_widgets][slider]"
) {
    SliderWorld test;
    auto& changes =
        test.world.resource<Events<ui_widgets::ValueChange<float>>>();
    changes.send({.source = test.slider, .value = 0.25f, .is_final = false});
    changes.send({.source = test.slider, .value = 0.75f, .is_final = true});

    test.world.run_system_once(ui_widgets::slider_self_update);

    CHECK(
        test.world.get_component<ui_widgets::SliderValue>(test.slider).value ==
        Catch::Approx(0.75f)
    );
}

TEST_CASE("SliderPlugin inserts required components", "[ui_widgets][slider]") {
    App app;
    app.add_resource(
        Window {.glfw_window = nullptr, .width = 200, .height = 120}
    );
    app.add_plugin<ui_widgets::SliderPlugin>();
    app.finish();

    const auto entity = app.world().entity();
    app.world().add_component(entity, ui_widgets::Slider {});
    const auto thumb = app.world().entity();
    app.world().add_component(thumb, ui_widgets::SliderThumb {});
    app.world().set_parent(thumb, entity);
    app.world().sort_systems();
    app.run_schedule(PostUpdate);

    CHECK(app.world().has_component<ui::Node>(entity));
    CHECK(app.world().has_component<ui::Interaction>(entity));
    CHECK(app.world().has_component<ui::FocusPolicy>(entity));
    CHECK(app.world().has_component<ui_widgets::SliderValue>(entity));
    CHECK(app.world().has_component<ui_widgets::SliderRange>(entity));
    CHECK(app.world().has_component<ui_widgets::SliderStep>(entity));
    CHECK(app.world().has_component<ui_widgets::SliderDragState>(entity));
    REQUIRE(app.world().has_component<ui::FocusPolicy>(thumb));
    CHECK(
        app.world().get_component<ui::FocusPolicy>(thumb) ==
        ui::FocusPolicy::Pass
    );
    CHECK_FALSE(app.world().has_component<input_focus::TabIndex>(entity));
}
