#include "ui_widgets/plugin.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
#include <string_view>
#include <vector>

namespace ets::ui_widgets {

namespace {

std::size_t clamp_boundary(std::string_view value, std::size_t position) {
    position = std::min(position, value.size());
    while (position > 0 && position < value.size() &&
           (static_cast<unsigned char>(value[position]) & 0xc0U) == 0x80U) {
        --position;
    }
    return position;
}

std::size_t next_boundary(std::string_view value, std::size_t position) {
    position = clamp_boundary(value, position);
    if (position >= value.size()) {
        return value.size();
    }
    ++position;
    while (position < value.size() &&
           (static_cast<unsigned char>(value[position]) & 0xc0U) == 0x80U) {
        ++position;
    }
    return position;
}

std::string encode_utf8(char32_t codepoint) {
    if (codepoint <= 0x7f) {
        return {static_cast<char>(codepoint)};
    }
    if (codepoint <= 0x7ff) {
        return {
            static_cast<char>(0xc0U | (codepoint >> 6U)),
            static_cast<char>(0x80U | (codepoint & 0x3fU)),
        };
    }
    if (codepoint <= 0xffff) {
        return {
            static_cast<char>(0xe0U | (codepoint >> 12U)),
            static_cast<char>(0x80U | ((codepoint >> 6U) & 0x3fU)),
            static_cast<char>(0x80U | (codepoint & 0x3fU)),
        };
    }
    if (codepoint <= 0x10ffff) {
        return {
            static_cast<char>(0xf0U | (codepoint >> 18U)),
            static_cast<char>(0x80U | ((codepoint >> 12U) & 0x3fU)),
            static_cast<char>(0x80U | ((codepoint >> 6U) & 0x3fU)),
            static_cast<char>(0x80U | (codepoint & 0x3fU)),
        };
    }
    return {};
}

std::vector<std::size_t> byte_boundaries(std::string_view value) {
    std::vector<std::size_t> result {0};
    auto position = std::size_t {0};
    while (position < value.size()) {
        position = next_boundary(value, position);
        result.push_back(position);
    }
    return result;
}

struct TextGeometry {
    const text::TextMeasureInfo* info {nullptr};
    text::TextLayout layout;
    std::vector<text::TextLine> lines;
    std::vector<std::size_t> boundaries;
    float width {0.0f};
};

std::optional<TextGeometry> text_geometry(
    const ui::Text& value,
    const ui::ComputedNode& computed,
    const ui::ContentSize& content_size
) {
    const auto* measure = std::get_if<ui::TextMeasure>(&content_size.measure);
    if (!measure || measure->info.line_height <= 0.0f) {
        return std::nullopt;
    }
    const Optional<float> max_width =
        measure->layout.line_break == text::LineBreak::NoWrap ?
            nullopt :
            Optional<float> {computed.content_size.x};
    return TextGeometry {
        .info = &measure->info,
        .layout = measure->layout,
        .lines = measure->info.lines(max_width, measure->layout.line_break),
        .boundaries = byte_boundaries(value.value),
        .width = computed.content_size.x,
    };
}

float line_origin(const TextGeometry& geometry, const text::TextLine& line) {
    const auto origin = geometry.layout.justify == text::Justify::Center ?
                            (geometry.width - line.width) * 0.5f :
                        geometry.layout.justify == text::Justify::Right ?
                            geometry.width - line.width :
                            0.0f;
    return std::max(0.0f, origin);
}

float glyph_position(
    const TextGeometry& geometry,
    const text::TextLine& line,
    std::size_t glyph
) {
    auto position = line_origin(geometry, line);
    glyph = std::clamp(glyph, line.begin, line.end);
    for (auto index = line.begin; index < glyph; ++index) {
        if (index > line.begin) {
            position += geometry.info->glyphs[index].kerning;
        }
        position += geometry.info->glyphs[index].advance;
    }
    return position;
}

std::size_t
byte_to_glyph(const std::vector<std::size_t>& boundaries, std::size_t byte) {
    return static_cast<std::size_t>(std::distance(
        boundaries.begin(),
        std::lower_bound(boundaries.begin(), boundaries.end(), byte)
    ));
}

std::size_t cursor_from_point(const TextGeometry& geometry, Vector2 point) {
    if (geometry.lines.empty()) {
        return 0;
    }
    const auto line_index = std::min(
        static_cast<std::size_t>(
            std::max(0.0f, std::floor(point.y / geometry.info->line_height))
        ),
        geometry.lines.size() - 1
    );
    const auto& line = geometry.lines[line_index];
    auto position = line_origin(geometry, line);
    for (auto index = line.begin; index < line.end; ++index) {
        const auto& glyph = geometry.info->glyphs[index];
        const auto cell_width =
            (index > line.begin ? glyph.kerning : 0.0f) + glyph.advance;
        if (point.x < position + cell_width * 0.5f) {
            return geometry.boundaries.at(index);
        }
        position += cell_width;
    }
    return geometry.boundaries.at(line.end);
}

void hide(ui::Node& node) {
    node.display = ui::Display::None;
}

void place(ui::Node& node, float left, float top, float width, float height) {
    node.display = ui::Display::Flex;
    node.position_type = ui::PositionType::Absolute;
    node.left = ui::px(left);
    node.top = ui::px(top);
    node.width = ui::px(width);
    node.height = ui::px(height);
}

} // namespace

void sync_text_inputs(
    Query<Entity, const TextInput>::Filter<Without<ui::Node>> missing_nodes,
    Query<Entity, const TextInput>::Filter<Without<ui::Text>> missing_text,
    Query<Entity, const TextInput>::Filter<Without<ui::Interaction>>
        missing_interactions,
    Query<Entity, const TextInput>::Filter<Without<ui::FocusPolicy>>
        missing_policies,
    Query<Entity, const TextInput>::Filter<Without<text::EditableText>>
        missing_editable_text,
    Query<Entity, const TextInput>::Filter<Without<TextInputState>>
        missing_states,
    Query<Entity, const TextCaret>::Filter<Without<ui::Node>>
        missing_caret_nodes,
    Query<Entity, const TextCaret>::Filter<Without<ui::FocusPolicy>>
        missing_caret_policies,
    Query<Entity, const TextSelection>::Filter<Without<ui::Node>>
        missing_selection_nodes,
    Query<Entity, const TextSelection>::Filter<Without<ui::FocusPolicy>>
        missing_selection_policies,
    Commands commands
) {
    for (const auto& [entity, input] : missing_nodes) {
        (void)input;
        commands.entity(entity).add(ui::Node {});
    }
    for (const auto& [entity, input] : missing_text) {
        (void)input;
        commands.entity(entity).add(ui::Text {});
    }
    for (const auto& [entity, input] : missing_interactions) {
        (void)input;
        commands.entity(entity).add(ui::Interaction::None);
    }
    for (const auto& [entity, input] : missing_policies) {
        (void)input;
        commands.entity(entity).add(ui::FocusPolicy::Block);
    }
    for (const auto& [entity, input] : missing_editable_text) {
        (void)input;
        commands.entity(entity).add(text::EditableText {});
    }
    for (const auto& [entity, input] : missing_states) {
        (void)input;
        commands.entity(entity).add(TextInputState {});
    }
    for (const auto& [entity, caret] : missing_caret_nodes) {
        (void)caret;
        commands.entity(entity).add(ui::Node {.display = ui::Display::None});
    }
    for (const auto& [entity, caret] : missing_caret_policies) {
        (void)caret;
        commands.entity(entity).add(ui::FocusPolicy::Pass);
    }
    for (const auto& [entity, selection] : missing_selection_nodes) {
        (void)selection;
        commands.entity(entity).add(ui::Node {.display = ui::Display::None});
    }
    for (const auto& [entity, selection] : missing_selection_policies) {
        (void)selection;
        commands.entity(entity).add(ui::FocusPolicy::Pass);
    }
}

void update_text_input_pointer(
    Query<
        Entity,
        const TextInput,
        const ui::Text,
        const ui::Interaction,
        const ui::ComputedNode,
        const ui::ContentSize,
        text::EditableText,
        TextInputState> inputs,
    Query<Entity, const ui::InteractionDisabled> disabled,
    ResRO<MouseInput> mouse,
    ResRO<KeyInput> keyboard
) {
    const auto selecting = keyboard->pressed(KeyCode::LeftShift) ||
                           keyboard->pressed(KeyCode::RightShift);
    for (auto
         [entity,
          input,
          value,
          interaction,
          computed,
          content,
          editable,
          state] : inputs) {
        (void)input;
        if (disabled.get(entity)) {
            state->pointer_dragging = false;
            state->select_all_on_release = false;
            continue;
        }
        const auto geometry = text_geometry(value, computed, content);
        if (!geometry) {
            continue;
        }
        const auto position = cursor_from_point(
            *geometry,
            mouse->position() - computed.content_position
        );
        if (mouse->just_pressed(MouseButton::Left) &&
            interaction == ui::Interaction::Pressed) {
            editable->queue(text::TextEdit::move_to(position, selecting));
            state->pointer_dragging = true;
            state->pointer_moved = false;
        } else if (
            state->pointer_dragging && mouse->pressed(MouseButton::Left) &&
            editable->cursor != position
        ) {
            editable->queue(text::TextEdit::move_to(position, true));
            state->pointer_moved = true;
            state->select_all_on_release = false;
        }
        if (state->pointer_dragging &&
            mouse->just_released(MouseButton::Left)) {
            if (state->select_all_on_release && !state->pointer_moved) {
                editable->queue(
                    text::TextEdit::move(text::TextEditKind::SelectAll)
                );
            }
            state->pointer_dragging = false;
            state->pointer_moved = false;
            state->select_all_on_release = false;
        }
    }
}

void update_text_input_decorations(
    Query<
        Entity,
        const TextInput,
        const ui::Text,
        const ui::ComputedNode,
        const ui::ContentSize,
        const text::EditableText> inputs,
    Query<Entity, ui::Node, const TextCaret> carets,
    Query<Entity, ui::Node, const TextSelection> selections,
    Query<Entity, const ChildOf> parents,
    ResRO<input_focus::InputFocus> input_focus,
    ResRO<Time> time
) {
    for (auto [caret_entity, node, caret] : carets) {
        (void)caret;
        const auto parent = parents.get(caret_entity);
        if (!parent) {
            hide(node.write());
            continue;
        }
        const auto input = inputs.get(std::get<1>(*parent).parent);
        if (!input ||
            input_focus->get() != Optional<Entity> {std::get<0>(*input)} ||
            std::fmod(time->elapsed_time(), 1.0f) >= 0.5f) {
            hide(node.write());
            continue;
        }
        const auto geometry = text_geometry(
            std::get<2>(*input),
            std::get<3>(*input),
            std::get<4>(*input)
        );
        if (!geometry || geometry->lines.empty()) {
            hide(node.write());
            continue;
        }
        const auto glyph =
            byte_to_glyph(geometry->boundaries, std::get<5>(*input).cursor);
        auto line_index = geometry->lines.size() - 1;
        for (std::size_t index = 0; index < geometry->lines.size(); ++index) {
            const auto& line = geometry->lines[index];
            if (glyph >= line.begin && glyph <= line.end) {
                line_index = index;
                break;
            }
        }
        const auto& line = geometry->lines[line_index];
        place(
            node.write(),
            glyph_position(*geometry, line, glyph),
            static_cast<float>(line_index) * geometry->info->line_height,
            1.0f,
            geometry->info->line_height
        );
    }

    for (auto [selection_entity, node, selection] : selections) {
        const auto parent = parents.get(selection_entity);
        if (!parent) {
            hide(node.write());
            continue;
        }
        const auto input = inputs.get(std::get<1>(*parent).parent);
        if (!input ||
            input_focus->get() != Optional<Entity> {std::get<0>(*input)} ||
            !std::get<5>(*input).has_selection()) {
            hide(node.write());
            continue;
        }
        const auto geometry = text_geometry(
            std::get<2>(*input),
            std::get<3>(*input),
            std::get<4>(*input)
        );
        if (!geometry) {
            hide(node.write());
            continue;
        }
        const auto begin = byte_to_glyph(
            geometry->boundaries,
            std::get<5>(*input).selection_start()
        );
        const auto end = byte_to_glyph(
            geometry->boundaries,
            std::get<5>(*input).selection_end()
        );
        auto segment = std::size_t {0};
        bool placed = false;
        for (std::size_t line_index = 0; line_index < geometry->lines.size();
             ++line_index) {
            const auto& line = geometry->lines[line_index];
            const auto segment_begin = std::max(begin, line.begin);
            const auto segment_end = std::min(end, line.end);
            if (segment_begin >= segment_end) {
                continue;
            }
            if (segment++ != selection.segment) {
                continue;
            }
            const auto left = glyph_position(*geometry, line, segment_begin);
            place(
                node.write(),
                left,
                static_cast<float>(line_index) * geometry->info->line_height,
                glyph_position(*geometry, line, segment_end) - left,
                geometry->info->line_height
            );
            placed = true;
            break;
        }
        if (!placed) {
            hide(node.write());
        }
    }
}

void update_text_inputs(
    Query<
        Entity,
        const TextInput,
        const ui::Text,
        text::EditableText,
        TextInputState> inputs,
    Query<Entity, const SelectAllOnFocus> select_all_on_focus,
    Query<Entity, const ui::InteractionDisabled> disabled,
    ResRO<KeyInput> keyboard,
    ResRO<CharacterInput> characters,
    ResRW<input_focus::InputFocus> input_focus,
    EventReader<input_focus::FocusGained> focus_gained,
    EventReader<input_focus::FocusLost> focus_lost,
    EventWriter<ValueChange<std::string>> changed,
    EventWriter<TextSubmit> submitted
) {
    while (const auto event = focus_gained.next()) {
        const auto item = inputs.get(event->entity);
        if (!item) {
            continue;
        }
        const auto& value = std::get<2>(*item);
        auto editable = std::get<3>(*item);
        auto state = std::get<4>(*item);
        if (event->cause == input_focus::FocusCause::Pointer) {
            state->select_all_on_release =
                static_cast<bool>(select_all_on_focus.get(event->entity));
        } else if (select_all_on_focus.get(event->entity)) {
            editable->queue(
                text::TextEdit::move(text::TextEditKind::SelectAll)
            );
        } else {
            editable->queue(text::TextEdit::move_to(value.value.size()));
        }
    }

    while (const auto event = focus_lost.next()) {
        const auto item = inputs.get(event->entity);
        if (!item) {
            continue;
        }
        const auto& value = std::get<2>(*item);
        auto editable = std::get<3>(*item);
        auto state = std::get<4>(*item);
        editable->queue(
            text::TextEdit::move(text::TextEditKind::CollapseSelection)
        );
        state->pointer_dragging = false;
        state->pointer_moved = false;
        state->select_all_on_release = false;
        if (state->changed) {
            changed.send(
                ValueChange<std::string> {
                    .source = event->entity,
                    .value = value.value,
                    .is_final = true,
                }
            );
            state->changed = false;
        }
    }

    if (!input_focus->get()) {
        return;
    }
    const auto entity = *input_focus->get();
    const auto item = inputs.get(entity);
    if (!item || disabled.get(entity)) {
        return;
    }

    const auto& value = std::get<2>(*item);
    auto editable = std::get<3>(*item);
    auto state = std::get<4>(*item);
    const bool selecting = keyboard->pressed(KeyCode::LeftShift) ||
                           keyboard->pressed(KeyCode::RightShift);
    const bool control = keyboard->pressed(KeyCode::LeftControl) ||
                         keyboard->pressed(KeyCode::RightControl);

    const bool keyboard_activity = !characters->characters().empty() ||
                                   keyboard->just_pressed(KeyCode::A) ||
                                   keyboard->just_pressed(KeyCode::Left) ||
                                   keyboard->just_pressed(KeyCode::Right) ||
                                   keyboard->just_pressed(KeyCode::Home) ||
                                   keyboard->just_pressed(KeyCode::End) ||
                                   keyboard->just_pressed(KeyCode::Backspace) ||
                                   keyboard->just_pressed(KeyCode::Delete) ||
                                   keyboard->just_pressed(KeyCode::Escape) ||
                                   keyboard->just_pressed(KeyCode::Enter);
    if (keyboard_activity) {
        state->select_all_on_release = false;
    }

    if (control && keyboard->just_pressed(KeyCode::A)) {
        editable->queue(text::TextEdit::move(text::TextEditKind::SelectAll));
    } else if (keyboard->just_pressed(KeyCode::Left)) {
        editable->queue(
            text::TextEdit::move(text::TextEditKind::MoveLeft, selecting)
        );
    } else if (keyboard->just_pressed(KeyCode::Right)) {
        editable->queue(
            text::TextEdit::move(text::TextEditKind::MoveRight, selecting)
        );
    } else if (keyboard->just_pressed(KeyCode::Home)) {
        editable->queue(
            text::TextEdit::move(text::TextEditKind::MoveStart, selecting)
        );
    } else if (keyboard->just_pressed(KeyCode::End)) {
        editable->queue(
            text::TextEdit::move(text::TextEditKind::MoveEnd, selecting)
        );
    } else if (keyboard->just_pressed(KeyCode::Backspace)) {
        editable->queue(text::TextEdit::move(text::TextEditKind::Backspace));
    } else if (keyboard->just_pressed(KeyCode::Delete)) {
        editable->queue(text::TextEdit::move(text::TextEditKind::Delete));
    } else if (keyboard->just_pressed(KeyCode::Escape)) {
        editable->queue(
            text::TextEdit::move(text::TextEditKind::CollapseSelection)
        );
        input_focus->clear();
    } else if (keyboard->just_pressed(KeyCode::Enter)) {
        if (editable->allow_newlines) {
            editable->queue(text::TextEdit::insert("\n"));
        } else {
            submitted.send(TextSubmit {.entity = entity, .value = value.value});
            changed.send(
                ValueChange<std::string> {
                    .source = entity,
                    .value = value.value,
                    .is_final = true,
                }
            );
            state->changed = false;
        }
    }

    for (const auto character : characters->characters()) {
        if (character < U' ' || character == U'\x7f') {
            continue;
        }
        const auto encoded = encode_utf8(character);
        if (encoded.empty()) {
            continue;
        }
        editable->queue(text::TextEdit::insert(encoded));
    }
}

void forward_text_input_changes(
    Query<Entity, const TextInput, TextInputState> inputs,
    EventReader<text::TextChanged> text_changed,
    EventWriter<ValueChange<std::string>> changed
) {
    while (const auto event = text_changed.next()) {
        const auto input = inputs.get(event->entity);
        if (!input) {
            continue;
        }
        auto state = std::get<2>(*input);
        state->changed = true;
        changed.send(
            ValueChange<std::string> {
                .source = event->entity,
                .value = event->value,
                .is_final = false,
            }
        );
    }
}

} // namespace ets::ui_widgets
