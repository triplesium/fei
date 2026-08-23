#include "text/editable.hpp"

#include <algorithm>
#include <string_view>

namespace ets::text {

namespace {

std::size_t clamp_boundary(std::string_view value, std::size_t position) {
    position = std::min(position, value.size());
    while (position > 0 && position < value.size() &&
           (static_cast<unsigned char>(value[position]) & 0xc0U) == 0x80U) {
        --position;
    }
    return position;
}

std::size_t previous_boundary(std::string_view value, std::size_t position) {
    position = clamp_boundary(value, position);
    if (position == 0) {
        return 0;
    }
    --position;
    while (position > 0 &&
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

void clamp_cursor(EditableText& editable, std::string_view value) {
    editable.anchor = clamp_boundary(value, editable.anchor);
    editable.cursor = clamp_boundary(value, editable.cursor);
}

void erase_selection(std::string& value, EditableText& editable) {
    if (!editable.has_selection()) {
        return;
    }
    const auto begin = editable.selection_start();
    value.erase(begin, editable.selection_end() - begin);
    editable.anchor = begin;
    editable.cursor = begin;
}

void move_cursor(EditableText& editable, std::size_t position, bool selecting) {
    editable.cursor = position;
    if (!selecting) {
        editable.anchor = position;
    }
}

bool apply_edit(
    std::string& value,
    EditableText& editable,
    const TextEdit& edit
) {
    switch (edit.kind) {
        case TextEditKind::Insert: {
            if (!editable.allow_newlines &&
                edit.value.find('\n') != std::string::npos) {
                return false;
            }
            erase_selection(value, editable);
            value.insert(editable.cursor, edit.value);
            editable.cursor += edit.value.size();
            editable.anchor = editable.cursor;
            return !edit.value.empty();
        }
        case TextEditKind::Backspace:
            if (editable.has_selection()) {
                erase_selection(value, editable);
                return true;
            }
            if (editable.cursor > 0) {
                const auto begin = previous_boundary(value, editable.cursor);
                value.erase(begin, editable.cursor - begin);
                editable.anchor = begin;
                editable.cursor = begin;
                return true;
            }
            return false;
        case TextEditKind::Delete:
            if (editable.has_selection()) {
                erase_selection(value, editable);
                return true;
            }
            if (editable.cursor < value.size()) {
                const auto end = next_boundary(value, editable.cursor);
                value.erase(editable.cursor, end - editable.cursor);
                return true;
            }
            return false;
        case TextEditKind::MoveLeft: {
            const auto position = !edit.selecting && editable.has_selection() ?
                                      editable.selection_start() :
                                      previous_boundary(value, editable.cursor);
            move_cursor(editable, position, edit.selecting);
            return false;
        }
        case TextEditKind::MoveRight: {
            const auto position = !edit.selecting && editable.has_selection() ?
                                      editable.selection_end() :
                                      next_boundary(value, editable.cursor);
            move_cursor(editable, position, edit.selecting);
            return false;
        }
        case TextEditKind::MoveStart:
            move_cursor(editable, 0, edit.selecting);
            return false;
        case TextEditKind::MoveEnd:
            move_cursor(editable, value.size(), edit.selecting);
            return false;
        case TextEditKind::MoveTo:
            move_cursor(
                editable,
                clamp_boundary(value, edit.position),
                edit.selecting
            );
            return false;
        case TextEditKind::SelectAll:
            editable.anchor = 0;
            editable.cursor = value.size();
            return false;
        case TextEditKind::CollapseSelection:
            editable.anchor = editable.cursor;
            return false;
    }
    return false;
}

} // namespace

std::size_t EditableText::selection_start() const {
    return std::min(anchor, cursor);
}

std::size_t EditableText::selection_end() const {
    return std::max(anchor, cursor);
}

void apply_text_edits(
    Query<Entity, Text, EditableText> texts,
    EventWriter<TextChanged> changed
) {
    for (auto [entity, value, editable] : texts) {
        clamp_cursor(editable.write(), value->value);
        bool value_changed = false;
        for (const auto& edit : editable->pending_edits) {
            value_changed |= apply_edit(value->value, editable.write(), edit);
        }
        editable->pending_edits.clear();
        if (value_changed) {
            changed.send(TextChanged {.entity = entity, .value = value->value});
        }
    }
}

} // namespace ets::text
