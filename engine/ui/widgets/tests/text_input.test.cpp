#include "ui_widgets/text_input.hpp"

#include "app/app.hpp"
#include "core/time.hpp"
#include "ecs/world.hpp"
#include "input_focus/focus.hpp"
#include "input_focus/tab_navigation.hpp"
#include "ui/plugin.hpp"
#include "ui/text.hpp"
#include "ui_widgets/plugin.hpp"
#include "ui_widgets/value_change.hpp"
#include "input/input.hpp"
#include "window/window.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace fei;

namespace {

struct TextInputWorld {
    World world;
    Entity entity;

    TextInputWorld(std::string value = "hello") {
        world.add_resource(KeyInput {});
        world.add_resource(CharacterInput {});
        world.add_resource(input_focus::InputFocus {});
        world.add_resource(Events<input_focus::FocusGained> {});
        world.add_resource(Events<input_focus::FocusLost> {});
        world.add_resource(Events<text::TextChanged> {});
        world.add_resource(Events<ui_widgets::ValueChange<std::string>> {});
        world.add_resource(Events<ui_widgets::TextSubmit> {});

        entity = world.entity();
        world.add_component(entity, ui_widgets::TextInput {});
        world.add_component(entity, ui::Text {.value = std::move(value)});
        world.add_component(entity, text::EditableText {});
        world.add_component(entity, ui_widgets::TextInputState {});
        world.resource<input_focus::InputFocus>().set(entity);
    }

    void update() {
        world.run_system_once(ui_widgets::update_text_inputs);
        world.run_system_once(text::apply_text_edits);
        world.run_system_once(ui_widgets::forward_text_input_changes);
    }

    KeyInput& keyboard() { return world.resource<KeyInput>(); }
    CharacterInput& characters() { return world.resource<CharacterInput>(); }

    const ui::Text& text() const {
        return world.get_component<ui::Text>(entity);
    }

    const text::EditableText& editable() const {
        return world.get_component<text::EditableText>(entity);
    }

    const ui_widgets::TextInputState& state() const {
        return world.get_component<ui_widgets::TextInputState>(entity);
    }
};

ui::ContentSize text_measure(std::string_view value) {
    text::TextMeasureInfo info {
        .min = {static_cast<float>(value.size()) * 10.0f, 20.0f},
        .max = {static_cast<float>(value.size()) * 10.0f, 20.0f},
        .ascent = 15.0f,
        .line_height = 20.0f,
    };
    for (const auto character : value) {
        info.glyphs.push_back(
            {.codepoint = static_cast<char32_t>(character), .advance = 10.0f}
        );
    }
    return ui::ContentSize {
        .measure = ui::TextMeasure {
            .info = std::move(info),
            .layout = text::TextLayout {.line_break = text::LineBreak::NoWrap},
        },
    };
}

struct PointerTextInputWorld : TextInputWorld {
    PointerTextInputWorld() : TextInputWorld("abcd") {
        world.add_resource(MouseInput {});
        world.add_component(entity, ui::Interaction::Pressed);
        world.add_component(
            entity,
            ui::ComputedNode {
                .content_size = {100.0f, 20.0f},
                .content_position = {10.0f, 10.0f},
            }
        );
        world.add_component(entity, text_measure("abcd"));
    }

    void update_pointer() {
        world.run_system_once(ui_widgets::update_text_input_pointer);
        world.run_system_once(text::apply_text_edits);
    }

    MouseInput& mouse() { return world.resource<MouseInput>(); }
};

} // namespace

TEST_CASE("TextInput inserts Unicode characters", "[ui_widgets][text_input]") {
    TextInputWorld test;
    test.world.add_component(
        test.entity,
        text::EditableText {.anchor = 5, .cursor = 5}
    );
    test.characters().push(U'\u754c');

    test.update();

    CHECK(test.text().value == "hello\xe7\x95\x8c");
    CHECK(test.editable().cursor == test.text().value.size());
    const auto event =
        test.world.resource<Events<ui_widgets::ValueChange<std::string>>>()
            .get_event(0);
    REQUIRE(event);
    CHECK(event->event.value == "hello\xe7\x95\x8c");
    CHECK_FALSE(event->event.is_final);
}

TEST_CASE(
    "TextInput Backspace removes a complete UTF-8 character",
    "[ui_widgets][text_input]"
) {
    TextInputWorld test("A\xe7\x95\x8c");
    test.world.add_component(
        test.entity,
        text::EditableText {
            .anchor = test.text().value.size(),
            .cursor = test.text().value.size(),
        }
    );
    test.keyboard().press(KeyCode::Backspace);

    test.update();

    CHECK(test.text().value == "A");
    CHECK(test.editable().cursor == 1);
}

TEST_CASE("TextInput replaces a selected range", "[ui_widgets][text_input]") {
    TextInputWorld test;
    test.world.add_component(
        test.entity,
        text::EditableText {.anchor = 1, .cursor = 4}
    );
    test.characters().push(U'a');

    test.update();

    CHECK(test.text().value == "hao");
    CHECK_FALSE(test.editable().has_selection());
    CHECK(test.editable().cursor == 2);
}

TEST_CASE(
    "TextInput supports Shift selection and collapse",
    "[ui_widgets][text_input]"
) {
    TextInputWorld test;
    test.world.add_component(
        test.entity,
        text::EditableText {.anchor = 5, .cursor = 5}
    );
    test.keyboard().press(KeyCode::LeftShift);
    test.keyboard().press(KeyCode::Left);

    test.update();

    CHECK(test.editable().anchor == 5);
    CHECK(test.editable().cursor == 4);

    test.keyboard().clear();
    test.update();
    test.keyboard().clear();
    test.keyboard().press(KeyCode::Left);
    test.update();
    CHECK(test.editable().anchor == 4);
    CHECK(test.editable().cursor == 4);
}

TEST_CASE(
    "Single-line TextInput submits on Enter",
    "[ui_widgets][text_input]"
) {
    TextInputWorld test;
    test.keyboard().press(KeyCode::Enter);

    test.update();

    const auto submit =
        test.world.resource<Events<ui_widgets::TextSubmit>>().get_event(0);
    REQUIRE(submit);
    CHECK(submit->event.entity == test.entity);
    CHECK(submit->event.value == "hello");
    const auto change =
        test.world.resource<Events<ui_widgets::ValueChange<std::string>>>()
            .get_event(0);
    REQUIRE(change);
    CHECK(change->event.is_final);
}

TEST_CASE(
    "Multiline TextInput inserts a newline on Enter",
    "[ui_widgets][text_input]"
) {
    TextInputWorld test;
    test.world.add_component(
        test.entity,
        text::EditableText {
            .anchor = 5,
            .cursor = 5,
            .allow_newlines = true,
        }
    );
    test.keyboard().press(KeyCode::Enter);

    test.update();

    CHECK(test.text().value == "hello\n");
    CHECK(test.world.resource<Events<ui_widgets::TextSubmit>>().size() == 0);
}

TEST_CASE(
    "SelectAllOnFocus selects the complete value",
    "[ui_widgets][text_input]"
) {
    TextInputWorld test;
    test.world.add_component(test.entity, ui_widgets::SelectAllOnFocus {});
    test.world.resource<Events<input_focus::FocusGained>>().send(
        {.entity = test.entity, .cause = input_focus::FocusCause::Navigation}
    );

    test.update();

    CHECK(test.editable().selection_start() == 0);
    CHECK(test.editable().selection_end() == 5);
}

TEST_CASE("Disabled TextInput ignores editing", "[ui_widgets][text_input]") {
    TextInputWorld test;
    test.world.add_component(test.entity, ui::InteractionDisabled {});
    test.characters().push(U'X');

    test.update();

    CHECK(test.text().value == "hello");
}

TEST_CASE(
    "TextInput positions its cursor from a pointer press",
    "[ui_widgets][text_input]"
) {
    PointerTextInputWorld test;
    test.mouse().set_position({26.0f, 15.0f});
    test.mouse().press(MouseButton::Left);

    test.update_pointer();

    CHECK(test.editable().anchor == 2);
    CHECK(test.editable().cursor == 2);
    CHECK(test.state().pointer_dragging);
}

TEST_CASE(
    "TextInput extends selection while pointer dragging",
    "[ui_widgets][text_input]"
) {
    PointerTextInputWorld test;
    test.mouse().set_position({14.0f, 15.0f});
    test.mouse().press(MouseButton::Left);
    test.update_pointer();

    test.mouse().clear();
    test.mouse().set_position({37.0f, 15.0f});
    test.mouse().press(MouseButton::Left);
    test.update_pointer();

    CHECK(test.editable().anchor == 0);
    CHECK(test.editable().cursor == 3);
    CHECK(test.state().pointer_moved);
}

TEST_CASE(
    "Shift pointer press preserves TextInput selection anchor",
    "[ui_widgets][text_input]"
) {
    PointerTextInputWorld test;
    test.world.add_component(
        test.entity,
        text::EditableText {.anchor = 1, .cursor = 1}
    );
    test.keyboard().press(KeyCode::LeftShift);
    test.mouse().set_position({38.0f, 15.0f});
    test.mouse().press(MouseButton::Left);

    test.update_pointer();

    CHECK(test.editable().anchor == 1);
    CHECK(test.editable().cursor == 3);
}

TEST_CASE(
    "TextInput decorations follow cursor and selection geometry",
    "[ui_widgets][text_input]"
) {
    PointerTextInputWorld test;
    test.world.add_resource(Time {});
    test.world.resource<Time>().reset_elapsed_time(0.1f);
    test.world.add_component(
        test.entity,
        text::EditableText {.anchor = 1, .cursor = 3}
    );
    const auto selection = test.world.entity();
    test.world.add_component(selection, ui_widgets::TextSelection {});
    test.world.add_component(selection, ui::Node {});
    test.world.set_parent(selection, test.entity);
    const auto caret = test.world.entity();
    test.world.add_component(caret, ui_widgets::TextCaret {});
    test.world.add_component(caret, ui::Node {});
    test.world.set_parent(caret, test.entity);

    test.world.run_system_once(ui_widgets::update_text_input_decorations);

    const auto& selection_node = test.world.get_component<ui::Node>(selection);
    CHECK(selection_node.display == ui::Display::Flex);
    CHECK(selection_node.left == ui::px(10.0f));
    CHECK(selection_node.width == ui::px(20.0f));
    const auto& caret_node = test.world.get_component<ui::Node>(caret);
    CHECK(caret_node.display == ui::Display::Flex);
    CHECK(caret_node.left == ui::px(30.0f));
    CHECK(caret_node.height == ui::px(20.0f));
}

TEST_CASE(
    "TextInputPlugin inserts required components",
    "[ui_widgets][text_input]"
) {
    App app;
    app.add_resource(
        Window {.glfw_window = nullptr, .width = 200, .height = 120}
    );
    app.add_plugin<ui_widgets::TextInputPlugin>();
    app.finish();

    const auto entity = app.world().entity();
    app.world().add_component(entity, ui_widgets::TextInput {});
    app.world().sort_systems();
    app.run_schedule(PostUpdate);

    CHECK(app.world().has_component<ui::Node>(entity));
    CHECK(app.world().has_component<ui::Text>(entity));
    CHECK(app.world().has_component<ui::Interaction>(entity));
    CHECK(app.world().has_component<ui::FocusPolicy>(entity));
    CHECK(app.world().has_component<text::EditableText>(entity));
    CHECK(app.world().has_component<ui_widgets::TextInputState>(entity));
    CHECK_FALSE(app.world().has_component<input_focus::TabIndex>(entity));

    app.world().add_component(entity, ui::Text {.value = "hello"});
    auto editable = app.world().get_component_rw<text::EditableText>(entity);
    editable->queue(text::TextEdit::move(text::TextEditKind::MoveEnd));
    editable->queue(text::TextEdit::insert("X"));
    app.run_schedule(PreUpdate);
    CHECK(app.world().get_component<ui::Text>(entity).value == "helloX");
    const auto change =
        app.resource<Events<ui_widgets::ValueChange<std::string>>>().get_event(
            0
        );
    REQUIRE(change);
    CHECK(change->event.value == "helloX");
    CHECK_FALSE(change->event.is_final);

    const auto caret = app.world().entity();
    app.world().add_component(caret, ui_widgets::TextCaret {});
    app.run_schedule(PostUpdate);
    CHECK(app.world().has_component<ui::Node>(caret));
    CHECK(
        app.world().get_component<ui::FocusPolicy>(caret) ==
        ui::FocusPolicy::Pass
    );
}
