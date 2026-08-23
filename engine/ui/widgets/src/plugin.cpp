#include "ui_widgets/plugin.hpp"

#include "app/app.hpp"
#include "ecs/system_config.hpp"
#include "ui/plugin.hpp"

namespace ets::ui_widgets {

void PopoverPlugin::dependencies(PluginDependencies& dependencies) const {
    dependencies.require<ui::UiPlugin>();
}

void PopoverPlugin::setup(App& app) {
    app.configure_sets(
           PostUpdate,
           Systems::Prepare {}.before<ui::Systems::Prepare>()
    )
        .add_systems(
            PostUpdate,
            chain(sync_popovers, update_popovers) | in_set<Systems::Prepare>()
        );
}

void MenuPlugin::dependencies(PluginDependencies& dependencies) const {
    dependencies.require<ButtonPlugin>().require<PopoverPlugin>();
}

void MenuPlugin::setup(App& app) {
    app.add_event<MenuEvent>()
        .configure_sets(PreUpdate, Systems::Changes {}.after<Systems::Update>())
        .add_systems(PreUpdate, update_menus | in_set<Systems::Changes>())
        .configure_sets(
            PostUpdate,
            Systems::Prepare {}.before<ui::Systems::Prepare>()
        )
        .add_systems(PostUpdate, sync_menus | in_set<Systems::Prepare>());
}

void ListBoxPlugin::dependencies(PluginDependencies& dependencies) const {
    dependencies.require<ButtonPlugin>();
}

void ListBoxPlugin::setup(App& app) {
    app.add_event<ValueChange<Entity>>()
        .add_event<SetListSelection>()
        .configure_sets(PreUpdate, Systems::Changes {}.after<Systems::Update>())
        .add_systems(PreUpdate, update_list_boxes | in_set<Systems::Changes>())
        .configure_sets(
            PostUpdate,
            Systems::Prepare {}.before<ui::Systems::Prepare>()
        )
        .add_systems(PostUpdate, sync_list_boxes | in_set<Systems::Prepare>());
}

void SelectPlugin::dependencies(PluginDependencies& dependencies) const {
    dependencies.require<ListBoxPlugin>()
        .require<PopoverPlugin>()
        .require<TextInputPlugin>();
}

void SelectPlugin::setup(App& app) {
    app.add_event<SelectionChange>()
        .configure_sets(
            PreUpdate,
            chain(
                Systems::Update {},
                Systems::Changes {},
                Systems::Selection {}
            )
        )
        .add_systems(PreUpdate, update_selects | in_set<Systems::Selection>())
        .configure_sets(
            PostUpdate,
            Systems::Prepare {}.before<ui::Systems::Prepare>()
        )
        .add_systems(PostUpdate, sync_selects | in_set<Systems::Prepare>());
}

void TooltipPlugin::dependencies(PluginDependencies& dependencies) const {
    dependencies.require<PopoverPlugin>().require<TimePlugin>();
}

void TooltipPlugin::setup(App& app) {
    app.configure_sets(
           PreUpdate,
           Systems::Update {}.after<ui::Systems::Focus>()
    )
        .add_systems(PreUpdate, update_tooltips | in_set<Systems::Update>())
        .configure_sets(
            PostUpdate,
            Systems::Prepare {}.before<ui::Systems::Prepare>()
        )
        .add_systems(PostUpdate, sync_tooltips | in_set<Systems::Prepare>());
}

void ButtonPlugin::dependencies(PluginDependencies& dependencies) const {
    dependencies.require<ui::UiPlugin>();
}

void ButtonPlugin::setup(App& app) {
    app.add_event<Activate>()
        .configure_sets(
            PreUpdate,
            Systems::Update {}.after<ui::Systems::Focus>()
        )
        .add_systems(PreUpdate, update_buttons | in_set<Systems::Update>())
        .configure_sets(
            PostUpdate,
            Systems::Prepare {}.before<ui::Systems::Prepare>()
        )
        .add_systems(PostUpdate, sync_buttons | in_set<Systems::Prepare>());
}

void CheckboxPlugin::dependencies(PluginDependencies& dependencies) const {
    dependencies.require<ui::UiPlugin>();
}

void CheckboxPlugin::setup(App& app) {
    app.add_event<ValueChange<bool>>()
        .add_event<SetChecked>()
        .add_event<ToggleChecked>()
        .configure_sets(
            PreUpdate,
            Systems::Update {}.after<ui::Systems::Focus>()
        )
        .add_systems(PreUpdate, update_checkboxes | in_set<Systems::Update>())
        .configure_sets(
            PostUpdate,
            Systems::Prepare {}.before<ui::Systems::Prepare>()
        )
        .add_systems(PostUpdate, sync_checkboxes | in_set<Systems::Prepare>());
}

void RadioGroupPlugin::dependencies(PluginDependencies& dependencies) const {
    dependencies.require<ui::UiPlugin>();
}

void RadioGroupPlugin::setup(App& app) {
    app.add_event<ValueChange<bool>>()
        .add_event<ValueChange<Entity>>()
        .configure_sets(
            PreUpdate,
            Systems::Update {}.after<ui::Systems::Focus>()
        )
        .add_systems(
            PreUpdate,
            chain(
                update_radio_buttons,
                navigate_radio_groups,
                propagate_radio_changes
            ) | in_set<Systems::Update>()
        )
        .configure_sets(
            PostUpdate,
            Systems::Prepare {}.before<ui::Systems::Prepare>()
        )
        .add_systems(
            PostUpdate,
            sync_radio_buttons | in_set<Systems::Prepare>()
        );
}

void SliderPlugin::dependencies(PluginDependencies& dependencies) const {
    dependencies.require<ui::UiPlugin>();
}

void SliderPlugin::setup(App& app) {
    app.add_event<ValueChange<float>>()
        .add_event<SetSliderValue>()
        .configure_sets(
            PreUpdate,
            Systems::Update {}.after<ui::Systems::Focus>()
        )
        .add_systems(PreUpdate, update_sliders | in_set<Systems::Update>())
        .configure_sets(
            PostUpdate,
            Systems::Prepare {}.before<ui::Systems::Prepare>()
        )
        .add_systems(PostUpdate, sync_sliders | in_set<Systems::Prepare>());
}

void ScrollAreaPlugin::dependencies(PluginDependencies& dependencies) const {
    dependencies.require<ui::UiPlugin>();
}

void ScrollAreaPlugin::setup(App& app) {
    app.add_event<ScrollIntoView>()
        .configure_sets(
            PreUpdate,
            Systems::Update {}.after<ui::Systems::Focus>()
        )
        .add_systems(PreUpdate, update_scroll_areas | in_set<Systems::Update>())
        .configure_sets(
            PostUpdate,
            Systems::Prepare {}.before<ui::Systems::Prepare>()
        )
        .add_systems(
            PostUpdate,
            sync_scroll_areas | in_set<Systems::Prepare>()
        );
}

void ScrollbarPlugin::dependencies(PluginDependencies& dependencies) const {
    dependencies.require<ui::UiPlugin>();
}

void ScrollbarPlugin::setup(App& app) {
    app.configure_sets(
           PreUpdate,
           Systems::Update {}.after<ui::Systems::Focus>()
    )
        .add_systems(PreUpdate, update_scrollbars | in_set<Systems::Update>())
        .configure_sets(
            PostUpdate,
            Systems::Prepare {}.before<ui::Systems::Prepare>()
        )
        .add_systems(PostUpdate, sync_scrollbars | in_set<Systems::Prepare>());
}

void TextInputPlugin::dependencies(PluginDependencies& dependencies) const {
    dependencies.require<ui::UiPlugin>().require<TimePlugin>();
}

void TextInputPlugin::setup(App& app) {
    app.add_event<ValueChange<std::string>>()
        .add_event<TextSubmit>()
        .configure_sets(
            PreUpdate,
            chain(
                Systems::Update {}.after<ui::Systems::Focus>(),
                text::EditableTextSystems::Apply {},
                Systems::Changes {}
            )
        )
        .add_systems(
            PreUpdate,
            chain(update_text_input_pointer, update_text_inputs) |
                in_set<Systems::Update>()
        )
        .add_systems(
            PreUpdate,
            forward_text_input_changes | in_set<Systems::Changes>()
        )
        .configure_sets(
            PostUpdate,
            Systems::Prepare {}.before<ui::Systems::Prepare>()
        )
        .add_systems(
            PostUpdate,
            chain(sync_text_inputs, update_text_input_decorations) |
                in_set<Systems::Prepare>()
        );
}

} // namespace ets::ui_widgets
