#pragma once

#include "app/plugin.hpp"
#include "core/time.hpp"
#include "ecs/commands.hpp"
#include "ecs/event.hpp"
#include "ecs/query.hpp"
#include "ecs/system_set.hpp"
#include "input_focus/focus.hpp"
#include "input_focus/tab_navigation.hpp"
#include "text/editable.hpp"
#include "ui/interaction.hpp"
#include "ui/measurement.hpp"
#include "ui/node.hpp"
#include "ui/text.hpp"
#include "ui_widgets/button.hpp"
#include "ui_widgets/checkbox.hpp"
#include "ui_widgets/list_box.hpp"
#include "ui_widgets/menu.hpp"
#include "ui_widgets/popover.hpp"
#include "ui_widgets/radio.hpp"
#include "ui_widgets/scroll_area.hpp"
#include "ui_widgets/scrollbar.hpp"
#include "ui_widgets/select.hpp"
#include "ui_widgets/slider.hpp"
#include "ui_widgets/text_input.hpp"
#include "ui_widgets/tooltip.hpp"
#include "ui_widgets/value_change.hpp"
#include "window/input.hpp"

namespace fei {

class App;

namespace ui_widgets {

struct Systems {
    struct Update : SystemSet<Update> {};
    struct Changes : SystemSet<Changes> {};
    struct Selection : SystemSet<Selection> {};
    struct Prepare : SystemSet<Prepare> {};
};

void sync_popovers(
    Query<Entity, const Popover>::Filter<Without<ui::Node>> missing_nodes,
    Commands commands
);

void update_popovers(
    Query<Entity, const Popover, ui::Node, const ui::ComputedNode> popovers,
    Query<Entity, const ui::ComputedNode> computed_nodes,
    Query<Entity, const ChildOf> parents,
    ResRO<Window> window
);

void sync_menus(
    Query<Entity, const MenuButton>::Filter<Without<Button>> missing_buttons,
    Query<Entity, const MenuButton>::Filter<Without<input_focus::TabIndex>>
        missing_button_indices,
    Query<Entity, const MenuPopup>::Filter<Without<ui::Node>> missing_popups,
    Query<Entity, const MenuItem>::Filter<Without<Button>> missing_items,
    Query<Entity, const MenuItem>::Filter<Without<input_focus::TabIndex>>
        missing_item_indices,
    Query<Entity, const MenuButton> menu_buttons,
    Query<Entity, const Popover> popovers,
    Commands commands
);

void update_menus(
    Query<Entity, const MenuButton> menu_buttons,
    Query<Entity, const MenuPopup, ui::Node> popups,
    Query<Entity, const MenuItem> menu_items,
    Query<Entity, const ui::Interaction> interactions,
    Query<Entity, const ui::InteractionDisabled> disabled,
    Query<Entity, const Children> child_lists,
    Query<Entity, const ChildOf> parents,
    ResRO<MouseInput> mouse,
    ResRO<KeyInput> keyboard,
    ResRW<input_focus::InputFocus> input_focus,
    ResRW<input_focus::InputFocusVisible> input_focus_visible,
    EventReader<Activate> activated,
    Commands commands,
    EventWriter<MenuEvent> menu_events
);

void sync_list_boxes(
    Query<Entity, const ListBox>::Filter<Without<ui::Node>> missing_box_nodes,
    Query<Entity, const ListBox>::Filter<Without<ui::Interaction>>
        missing_box_interactions,
    Query<Entity, const ListBox>::Filter<Without<ui::FocusPolicy>>
        missing_box_policies,
    Query<Entity, const ListBox>::Filter<Without<input_focus::TabIndex>>
        missing_box_indices,
    Query<Entity, const ListBox>::Filter<Without<ActiveDescendant>>
        missing_active_descendants,
    Query<Entity, const ListItem>::Filter<Without<Button>> missing_item_buttons,
    Query<Entity, const ListItem>::Filter<Without<input_focus::TabIndex>>
        missing_item_indices,
    Query<Entity, const ListItem>::Filter<Without<ui::Selectable>>
        missing_selectable,
    Commands commands
);

void update_list_boxes(
    Query<Entity, const ListBox, ActiveDescendant> list_boxes,
    Query<Entity, const ListItem> items,
    Query<Entity, const ui::Interaction> interactions,
    Query<Entity, const ui::Selected> selected,
    Query<Entity, const ui::InteractionDisabled> disabled,
    Query<Entity, const Children> child_lists,
    Query<Entity, const ChildOf> parents,
    ResRO<KeyInput> keyboard,
    ResRO<input_focus::InputFocus> input_focus,
    EventReader<Activate> activated,
    EventReader<SetListSelection> set_selection,
    EventWriter<ValueChange<Entity>> changed
);

void list_box_self_update(
    EventReader<ValueChange<Entity>> changes,
    Query<Entity, const ListBox> list_boxes,
    Query<Entity, const ListItem> items,
    Query<Entity, const ui::Selected> selected,
    Query<Entity, const ChildOf> parents,
    Commands commands
);

void sync_selects(
    Query<Entity, const Select>::Filter<Without<Button>> missing_select_buttons,
    Query<Entity, const Select>::Filter<Without<input_focus::TabIndex>>
        missing_select_indices,
    Query<Entity, const ComboBox>::Filter<Without<TextInput>>
        missing_combo_inputs,
    Query<Entity, const ComboBox>::Filter<Without<input_focus::TabIndex>>
        missing_combo_indices,
    Query<Entity, const Select> selects,
    Query<Entity, const ComboBox> combo_boxes,
    Query<Entity, const Popover> popovers,
    Query<Entity, const ui::Node> nodes,
    Commands commands
);

void update_selects(
    Query<Entity, const Select> selects,
    Query<Entity, const ComboBox, const ui::Interaction> combo_boxes,
    Query<Entity, ui::Node> nodes,
    Query<Entity, const ListBox> list_boxes,
    Query<Entity, const ListItem> items,
    Query<Entity, const ui::Selected> selected,
    Query<Entity, const ui::Interaction> interactions,
    Query<Entity, ActiveDescendant> active_descendants,
    Query<Entity, const ChildOf> parents,
    ResRO<MouseInput> mouse,
    ResRO<KeyInput> keyboard,
    ResRW<input_focus::InputFocus> input_focus,
    ResRW<input_focus::InputFocusVisible> input_focus_visible,
    EventReader<Activate> activated,
    EventReader<ValueChange<Entity>> list_changes,
    Commands commands,
    EventWriter<SelectionChange> selection_changes
);

void sync_tooltips(
    Query<Entity, const Tooltip>::Filter<Without<ui::Interaction>>
        missing_interactions,
    Query<Entity, const Tooltip>::Filter<Without<TooltipState>> missing_states,
    Query<Entity, const Tooltip> tooltips,
    Query<Entity, const Popover> popovers,
    Query<Entity, ui::Node> nodes,
    Commands commands
);

void update_tooltips(
    Query<Entity, const Tooltip, const ui::Interaction, TooltipState> tooltips,
    Query<Entity, ui::Node> nodes,
    ResRO<Time> time
);

void sync_buttons(
    Query<Entity, const Button>::Filter<Without<ui::Node>> missing_nodes,
    Query<Entity, const Button>::Filter<Without<ui::Interaction>>
        missing_interactions,
    Query<Entity, const Button>::Filter<Without<ui::FocusPolicy>>
        missing_policies,
    Commands commands
);

void update_buttons(
    Query<Entity, const Button, const ui::Interaction> buttons,
    Query<Entity, const ui::Pressed> pressed,
    Query<Entity, const ui::InteractionDisabled> disabled,
    Query<Entity, const ActivateOnPress> activate_on_press,
    ResRO<MouseInput> mouse,
    ResRO<KeyInput> keyboard,
    ResRO<input_focus::InputFocus> input_focus,
    Commands commands,
    EventWriter<Activate> activated
);

void sync_checkboxes(
    Query<Entity, const Checkbox>::Filter<Without<ui::Node>> missing_nodes,
    Query<Entity, const Checkbox>::Filter<Without<ui::Interaction>>
        missing_interactions,
    Query<Entity, const Checkbox>::Filter<Without<ui::FocusPolicy>>
        missing_policies,
    Query<Entity, const Checkbox>::Filter<Without<ui::Checkable>>
        missing_checkable,
    Commands commands
);

void update_checkboxes(
    Query<Entity, const Checkbox, const ui::Interaction> checkboxes,
    Query<Entity, const ui::Checked> checked,
    Query<Entity, const ui::Pressed> pressed,
    Query<Entity, const ui::InteractionDisabled> disabled,
    ResRO<MouseInput> mouse,
    ResRO<KeyInput> keyboard,
    ResRO<input_focus::InputFocus> input_focus,
    EventReader<SetChecked> set_checked,
    EventReader<ToggleChecked> toggle_checked,
    Commands commands,
    EventWriter<ValueChange<bool>> changed
);

void checkbox_self_update(
    EventReader<ValueChange<bool>> changes,
    Query<Entity, const Checkbox> checkboxes,
    Query<Entity, const ui::Checked> checked,
    Commands commands
);

void sync_radio_buttons(
    Query<Entity, const RadioButton>::Filter<Without<ui::Node>> missing_nodes,
    Query<Entity, const RadioButton>::Filter<Without<ui::Interaction>>
        missing_interactions,
    Query<Entity, const RadioButton>::Filter<Without<ui::FocusPolicy>>
        missing_policies,
    Query<Entity, const RadioButton>::Filter<Without<ui::Checkable>>
        missing_checkable,
    Commands commands
);

void update_radio_buttons(
    Query<Entity, const RadioButton, const ui::Interaction> radio_buttons,
    Query<Entity, const ui::Checked> checked,
    Query<Entity, const ui::Pressed> pressed,
    Query<Entity, const ui::InteractionDisabled> disabled,
    ResRO<MouseInput> mouse,
    ResRO<KeyInput> keyboard,
    ResRO<input_focus::InputFocus> input_focus,
    Commands commands,
    EventWriter<ValueChange<bool>> changed
);

void navigate_radio_groups(
    Query<Entity, const RadioGroup> groups,
    Query<Entity, const RadioButton> radio_buttons,
    Query<Entity, const ui::Checked> checked,
    Query<Entity, const ui::InteractionDisabled> disabled,
    Query<Entity, const Children> child_lists,
    ResRO<KeyInput> keyboard,
    ResRO<input_focus::InputFocus> input_focus,
    EventWriter<ValueChange<bool>> changed
);

void propagate_radio_changes(
    EventReader<ValueChange<bool>> changes,
    Query<Entity, const RadioButton> radio_buttons,
    Query<Entity, const RadioGroup> groups,
    Query<Entity, const ui::InteractionDisabled> disabled,
    Query<Entity, const ChildOf> parents,
    EventWriter<ValueChange<Entity>> selected
);

void radio_self_update(
    EventReader<ValueChange<Entity>> changes,
    Query<Entity, const RadioGroup> groups,
    Query<Entity, const RadioButton> radio_buttons,
    Query<Entity, const ui::Checked> checked,
    Query<Entity, const ui::InteractionDisabled> disabled,
    Query<Entity, const Children> child_lists,
    Query<Entity, const ChildOf> parents,
    Commands commands
);

void sync_sliders(
    Query<Entity, const Slider>::Filter<Without<ui::Node>> missing_nodes,
    Query<Entity, const Slider>::Filter<Without<ui::Interaction>>
        missing_interactions,
    Query<Entity, const Slider>::Filter<Without<ui::FocusPolicy>>
        missing_policies,
    Query<Entity, const Slider>::Filter<Without<SliderValue>> missing_values,
    Query<Entity, const Slider>::Filter<Without<SliderRange>> missing_ranges,
    Query<Entity, const Slider>::Filter<Without<SliderStep>> missing_steps,
    Query<Entity, const Slider>::Filter<Without<SliderDragState>>
        missing_drag_states,
    Query<Entity, const SliderThumb>::Filter<Without<ui::FocusPolicy>>
        missing_thumb_policies,
    Commands commands
);

void update_sliders(
    Query<
        Entity,
        const Slider,
        const SliderValue,
        const SliderRange,
        const SliderStep,
        const ui::Interaction,
        SliderDragState> sliders,
    Query<Entity, const ui::ComputedNode> computed_nodes,
    Query<Entity, const SliderThumb, const ui::ComputedNode> thumbs,
    Query<Entity, const Children> child_lists,
    Query<Entity, const SliderPrecision> precisions,
    Query<Entity, const ui::InteractionDisabled> disabled,
    ResRO<MouseInput> mouse,
    ResRO<KeyInput> keyboard,
    ResRO<input_focus::InputFocus> input_focus,
    EventReader<SetSliderValue> set_values,
    Commands commands,
    EventWriter<ValueChange<float>> changed
);

void slider_self_update(
    EventReader<ValueChange<float>> changes,
    Query<Entity, const Slider> sliders,
    Commands commands
);

void sync_scroll_areas(
    Query<Entity, const ScrollArea>::Filter<Without<ui::Node>> missing_nodes,
    Query<Entity, const ScrollArea>::Filter<Without<ui::ScrollPosition>>
        missing_scroll_positions,
    Query<Entity, const ScrollArea>::Filter<Without<ui::RelativeCursorPosition>>
        missing_cursor_positions,
    Commands commands
);

void update_scroll_areas(
    Query<
        Entity,
        const ScrollArea,
        const ui::Node,
        ui::ScrollPosition,
        const ui::ComputedNode,
        const ui::RelativeCursorPosition> areas,
    Query<Entity, const ui::ComputedNode> computed_nodes,
    Query<Entity, const ChildOf> parents,
    ResRO<MouseScrollInput> mouse_scroll,
    EventReader<ScrollIntoView> scroll_into_view
);

void sync_scrollbars(
    Query<Entity, const Scrollbar>::Filter<Without<ui::Node>> missing_nodes,
    Query<Entity, const Scrollbar>::Filter<Without<ui::Interaction>>
        missing_interactions,
    Query<Entity, const Scrollbar>::Filter<Without<ui::FocusPolicy>>
        missing_policies,
    Query<Entity, const ScrollbarThumb>::Filter<Without<ui::Node>>
        missing_thumb_nodes,
    Query<Entity, const ScrollbarThumb>::Filter<Without<ui::Interaction>>
        missing_thumb_interactions,
    Query<Entity, const ScrollbarThumb>::Filter<Without<ui::FocusPolicy>>
        missing_thumb_policies,
    Query<Entity, const ScrollbarThumb>::Filter<Without<ScrollbarDragState>>
        missing_drag_states,
    Commands commands
);

void update_scrollbars(
    Query<
        Entity,
        const Scrollbar,
        const ui::Interaction,
        const ui::ComputedNode> scrollbars,
    Query<Entity, const ScrollbarThumb> thumbs,
    Query<
        Entity,
        ui::Node,
        const ui::Interaction,
        const ui::ComputedNode,
        ScrollbarDragState> thumb_nodes,
    Query<Entity, const Children> child_lists,
    Query<Entity, ui::ScrollPosition, const ui::ComputedNode> targets,
    ResRO<MouseInput> mouse,
    Commands commands
);

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
);

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
);

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
);

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
);

void forward_text_input_changes(
    Query<Entity, const TextInput, TextInputState> inputs,
    EventReader<text::TextChanged> text_changed,
    EventWriter<ValueChange<std::string>> changed
);

FEI_REFLECT(Plugin)
class ButtonPlugin : public fei::Plugin {
  public:
    void dependencies(PluginDependencies& dependencies) const override;
    void setup(App& app) override;
};

FEI_REFLECT(Plugin)
class CheckboxPlugin : public fei::Plugin {
  public:
    void dependencies(PluginDependencies& dependencies) const override;
    void setup(App& app) override;
};

FEI_REFLECT(Plugin)
class RadioGroupPlugin : public fei::Plugin {
  public:
    void dependencies(PluginDependencies& dependencies) const override;
    void setup(App& app) override;
};

FEI_REFLECT(Plugin)
class SliderPlugin : public fei::Plugin {
  public:
    void dependencies(PluginDependencies& dependencies) const override;
    void setup(App& app) override;
};

FEI_REFLECT(Plugin)
class ScrollAreaPlugin : public fei::Plugin {
  public:
    void dependencies(PluginDependencies& dependencies) const override;
    void setup(App& app) override;
};

FEI_REFLECT(Plugin)
class ScrollbarPlugin : public fei::Plugin {
  public:
    void dependencies(PluginDependencies& dependencies) const override;
    void setup(App& app) override;
};

FEI_REFLECT(Plugin)
class TextInputPlugin : public fei::Plugin {
  public:
    void dependencies(PluginDependencies& dependencies) const override;
    void setup(App& app) override;
};

FEI_REFLECT(Plugin)
class PopoverPlugin : public fei::Plugin {
  public:
    void dependencies(PluginDependencies& dependencies) const override;
    void setup(App& app) override;
};

FEI_REFLECT(Plugin)
class MenuPlugin : public fei::Plugin {
  public:
    void dependencies(PluginDependencies& dependencies) const override;
    void setup(App& app) override;
};

FEI_REFLECT(Plugin)
class ListBoxPlugin : public fei::Plugin {
  public:
    void dependencies(PluginDependencies& dependencies) const override;
    void setup(App& app) override;
};

FEI_REFLECT(Plugin)
class SelectPlugin : public fei::Plugin {
  public:
    void dependencies(PluginDependencies& dependencies) const override;
    void setup(App& app) override;
};

FEI_REFLECT(Plugin)
class TooltipPlugin : public fei::Plugin {
  public:
    void dependencies(PluginDependencies& dependencies) const override;
    void setup(App& app) override;
};

} // namespace ui_widgets
} // namespace fei
