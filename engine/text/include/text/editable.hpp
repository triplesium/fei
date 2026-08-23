#pragma once

#include "ecs/event.hpp"
#include "ecs/query.hpp"
#include "ecs/system_set.hpp"
#include "refl/reflect.hpp"
#include "text/text.hpp"

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace ets::text {

ETS_REFLECT()
enum class TextEditKind {
    Insert,
    Backspace,
    Delete,
    MoveLeft,
    MoveRight,
    MoveStart,
    MoveEnd,
    MoveTo,
    SelectAll,
    CollapseSelection,
};

ETS_REFLECT()
struct TextEdit {
    TextEditKind kind {TextEditKind::CollapseSelection};
    std::string value;
    std::size_t position {0};
    bool selecting {false};

    [[nodiscard]] static TextEdit insert(std::string value) {
        return {.kind = TextEditKind::Insert, .value = std::move(value)};
    }
    [[nodiscard]] static TextEdit
    move(TextEditKind kind, bool selecting = false) {
        return {.kind = kind, .selecting = selecting};
    }
    [[nodiscard]] static TextEdit
    move_to(std::size_t position, bool selecting = false) {
        return {
            .kind = TextEditKind::MoveTo,
            .position = position,
            .selecting = selecting,
        };
    }
};

ETS_REFLECT(Component)
struct EditableText {
    std::size_t anchor {0};
    std::size_t cursor {0};
    bool allow_newlines {false};
    std::vector<TextEdit> pending_edits;

    [[nodiscard]] bool has_selection() const { return anchor != cursor; }
    [[nodiscard]] std::size_t selection_start() const;
    [[nodiscard]] std::size_t selection_end() const;

    void queue(TextEdit edit) { pending_edits.push_back(std::move(edit)); }
};

ETS_REFLECT()
struct TextChanged {
    Entity entity;
    std::string value;

    bool operator==(const TextChanged&) const = default;
};

struct EditableTextSystems {
    struct Apply : SystemSet<Apply> {};
};

void apply_text_edits(
    Query<Entity, Text, EditableText> texts,
    EventWriter<TextChanged> changed
);

} // namespace ets::text
