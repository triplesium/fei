#include "input_focus/plugin.hpp"

#include "app/app.hpp"
#include "ecs/system_config.hpp"
#include "input_focus/focus.hpp"
#include "input_focus/tab_navigation.hpp"

namespace ets::input_focus {

void InputFocusPlugin::dependencies(PluginDependencies& dependencies) const {
    dependencies.require<InputPlugin>();
}

void InputFocusPlugin::setup(App& app) {
    app.add_event<FocusGained>()
        .add_event<FocusLost>()
        .add_resource(InputFocus {})
        .add_resource(InputFocusVisible {})
        .configure_sets(
            PreUpdate,
            chain(
                InputSystems::ApplyDevtools {},
                Systems::Validate {},
                Systems::AutoFocus {},
                Systems::Navigation {}
            ),
            Systems::Navigation {}.after<InputSystems::Update>()
        )
        .add_systems(
            PreUpdate,
            clear_invalid_focus | in_set<Systems::Validate>(),
            apply_auto_focus | in_set<Systems::AutoFocus>(),
            navigate_focus | in_set<Systems::Navigation>()
        )
        .configure_sets(PostUpdate, Systems::FocusChanges {})
        .add_systems(
            PostUpdate,
            process_focus_changes | in_set<Systems::FocusChanges>()
        );
}

} // namespace ets::input_focus
