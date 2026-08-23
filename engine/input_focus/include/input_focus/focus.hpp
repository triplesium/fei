#pragma once

#include "base/optional.hpp"
#include "ecs/event.hpp"
#include "ecs/fwd.hpp"
#include "ecs/hierarchy.hpp"
#include "ecs/query.hpp"
#include "ecs/system_params.hpp"
#include "ecs/system_set.hpp"
#include "refl/reflect.hpp"

namespace ets::input_focus {

ETS_REFLECT()
enum class FocusCause {
    Pointer,
    Navigation,
    Programmatic,
};

ETS_REFLECT()
struct FocusGained {
    Entity entity;
    FocusCause cause {FocusCause::Programmatic};

    bool operator==(const FocusGained&) const = default;
};

ETS_REFLECT()
struct FocusLost {
    Entity entity;

    bool operator==(const FocusLost&) const = default;
};

ETS_REFLECT(Resource)
struct InputFocus {
    Optional<Entity> entity;
    Optional<Entity> notified_entity;
    FocusCause cause {FocusCause::Programmatic};

    void set(Entity value, FocusCause value_cause = FocusCause::Programmatic) {
        if (entity != Optional<Entity> {value}) {
            entity = value;
            cause = value_cause;
        }
    }
    void clear(FocusCause value_cause = FocusCause::Programmatic) {
        if (entity) {
            entity = nullopt;
            cause = value_cause;
        }
    }
    [[nodiscard]] Optional<Entity> get() const { return entity; }
};

ETS_REFLECT(Resource)
struct InputFocusVisible {
    bool visible {false};
};

ETS_REFLECT(Component)
struct AutoFocus {};

struct Systems {
    struct Validate : SystemSet<Validate> {};
    struct AutoFocus : SystemSet<AutoFocus> {};
    struct Navigation : SystemSet<Navigation> {};
    struct FocusChanges : SystemSet<FocusChanges> {};
};

void clear_invalid_focus(Query<Entity> entities, ResRW<InputFocus> focus);

void apply_auto_focus(
    Query<Entity, const AutoFocus>::Filter<Added<AutoFocus>> added,
    ResRW<InputFocus> focus
);

void process_focus_changes(
    ResRW<InputFocus> focus,
    EventWriter<FocusGained> gained,
    EventWriter<FocusLost> lost
);

[[nodiscard]] bool is_focused(const InputFocus& focus, Entity entity);

[[nodiscard]] bool is_focus_within(
    const InputFocus& focus,
    Entity entity,
    const Query<Entity, const ChildOf>& parents
);

[[nodiscard]] bool is_focus_visible(
    const InputFocus& focus,
    const InputFocusVisible& visible,
    Entity entity
);

[[nodiscard]] bool is_focus_within_visible(
    const InputFocus& focus,
    const InputFocusVisible& visible,
    Entity entity,
    const Query<Entity, const ChildOf>& parents
);

} // namespace ets::input_focus
