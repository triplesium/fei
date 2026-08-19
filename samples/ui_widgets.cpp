#include "app/app.hpp"
#include "app/reflection_plugin.hpp"
#include "asset/server.hpp"
#include "asset/source.hpp"
#include "base/log.hpp"
#include "core/transform.hpp"
#include "devtools/plugin.hpp"
#include "devtools_input/plugin.hpp"
#include "devtools_rendering/plugin.hpp"
#include "ecs/commands.hpp"
#include "graphics_opengl_glfw/plugin.hpp"
#include "input_focus/tab_navigation.hpp"
#include "sprite/components.hpp"
#include "sprite/plugin.hpp"
#include "text/editable.hpp"
#include "text/font.hpp"
#include "text/text.hpp"
#include "ui/plugin.hpp"
#include "ui_rendering/plugin.hpp"
#include "ui_widgets/button.hpp"
#include "ui_widgets/checkbox.hpp"
#include "ui_widgets/list_box.hpp"
#include "ui_widgets/menu.hpp"
#include "ui_widgets/plugin.hpp"
#include "ui_widgets/popover.hpp"
#include "ui_widgets/radio.hpp"
#include "ui_widgets/select.hpp"
#include "ui_widgets/slider.hpp"
#include "ui_widgets/text_input.hpp"
#include "ui_widgets/tooltip.hpp"
#include "window/window.hpp"

#include <algorithm>
#include <string>
#include <string_view>
#include <tuple>

using namespace fei;

namespace {

const Color4F WINDOW_BACKGROUND {0.08f, 0.08f, 0.09f, 1.0f};
const Color4F PANEL_BACKGROUND {0.11f, 0.11f, 0.12f, 1.0f};
const Color4F NORMAL_BUTTON {0.15f, 0.15f, 0.15f, 1.0f};
const Color4F HOVERED_BUTTON {0.25f, 0.25f, 0.25f, 1.0f};
const Color4F PRESSED_BUTTON {0.35f, 0.75f, 0.35f, 1.0f};
const Color4F SLIDER_TRACK {0.05f, 0.05f, 0.05f, 1.0f};
const Color4F TEXT_COLOR {0.9f, 0.9f, 0.9f, 1.0f};
const Color4F DIM_TEXT {0.62f, 0.62f, 0.62f, 1.0f};
const Color4F BLACK {0.0f, 0.0f, 0.0f, 1.0f};
const Color4F WHITE {1.0f, 1.0f, 1.0f, 1.0f};
const Color4F RED {0.9f, 0.18f, 0.18f, 1.0f};
const Color4F DISABLED {0.32f, 0.32f, 0.32f, 1.0f};

FEI_REFLECT(Component)
struct DemoButton {};

FEI_REFLECT(Component)
struct DemoMenuItem {};

FEI_REFLECT(Component)
struct DemoListItem {};

FEI_REFLECT(Component)
struct DemoSliderThumb {};

FEI_REFLECT(Component)
struct OptionText {
    std::string value;
};

struct Options {
    bool devtools {false};
};

Options parse_arguments(int argc, char** argv) {
    Options result;
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument {argv[index]};
        if (argument == "--devtools") {
            result.devtools = true;
        } else {
            fatal("Unknown sample-ui-widgets argument {}", argument);
        }
    }
    return result;
}

Entity
spawn_panel(Commands& commands, Entity parent, ui::Node node, Color4F color) {
    auto entity = commands.spawn();
    entity.add(node, ui::BackgroundColor {.color = color}).set_parent(parent);
    return entity.id();
}

Entity spawn_text(
    Commands& commands,
    Entity parent,
    std::string value,
    Handle<text::Font> font,
    float size,
    Color4F color = TEXT_COLOR,
    ui::Node node = {}
) {
    auto entity = commands.spawn();
    entity
        .add(
            node,
            ui::Text {.value = std::move(value)},
            text::TextFont {.font = font, .font_size = size},
            text::TextColor {.color = color}
        )
        .set_parent(parent);
    return entity.id();
}

void add_text_input_decorations(Commands& commands, Entity input) {
    const auto selection = spawn_panel(
        commands,
        input,
        ui::Node {
            .display = ui::Display::None,
            .position_type = ui::PositionType::Absolute,
        },
        {0.25f, 0.55f, 0.95f, 0.38f}
    );
    commands.entity(selection).add(ui_widgets::TextSelection {});
    const auto caret = spawn_panel(
        commands,
        input,
        ui::Node {
            .display = ui::Display::None,
            .position_type = ui::PositionType::Absolute,
        },
        PRESSED_BUTTON
    );
    commands.entity(caret).add(ui_widgets::TextCaret {});
}

Entity spawn_list_item(
    Commands& commands,
    Entity parent,
    std::string label,
    Handle<text::Font> font
) {
    const auto item = spawn_panel(
        commands,
        parent,
        ui::Node {
            .height = ui::px(34.0f),
            .padding = ui::axes(ui::px(10.0f), ui::px(7.0f)),
            .flex_shrink = 0.0f,
        },
        NORMAL_BUTTON
    );
    commands.entity(item).add(
        ui_widgets::ListItem {},
        DemoListItem {},
        OptionText {.value = label},
        ui::Text {.value = std::move(label)},
        text::TextFont {.font = font, .font_size = 16.0f},
        text::TextColor {.color = TEXT_COLOR},
        ui::BorderRadius::all(ui::px(4.0f))
    );
    return item;
}

void setup(Commands commands, ResRW<AssetServer> assets) {
    assets->emplace_source<FilesystemAssetSource>(
        "ui-widget-fonts",
        FEI_ASSETS_PATH "/../engine/imgui/fonts"
    );
    const auto font =
        assets->load<text::Font>("ui-widget-fonts://Cousine-Regular.ttf");

    commands.spawn().add(
        Camera2d {.clear_color = WINDOW_BACKGROUND},
        Transform2d {}
    );

    auto root_commands = commands.spawn();
    root_commands.add(
        ui::Node {
            .padding = ui::all(ui::px(24.0f)),
            .align_items = ui::AlignItems::Center,
            .justify_content = ui::JustifyContent::Center,
        },
        input_focus::TabGroup {}
    );
    const auto root = root_commands.id();

    auto card_commands = commands.spawn();
    card_commands
        .add(
            ui::Node {
                .width = ui::px(760.0f),
                .height = ui::px(660.0f),
                .padding = ui::all(ui::px(24.0f)),
                .gap = ui::px(16.0f),
            },
            ui::BackgroundColor {.color = PANEL_BACKGROUND},
            ui::BorderRadius::all(ui::px(14.0f))
        )
        .set_parent(root);
    const auto card = card_commands.id();

    spawn_text(
        commands,
        card,
        "Standard Widgets",
        font,
        28.0f,
        TEXT_COLOR,
        ui::Node {.height = ui::px(34.0f), .flex_shrink = 0.0f}
    );

    auto columns_commands = commands.spawn();
    columns_commands
        .add(
            ui::Node {
                .flex_direction = ui::FlexDirection::Row,
                .flex_grow = 1.0f,
                .gap = ui::px(28.0f),
            }
        )
        .set_parent(card);
    const auto columns = columns_commands.id();

    const auto left = spawn_panel(
        commands,
        columns,
        ui::Node {
            .width = ui::px(338.0f),
            .padding = ui::all(ui::px(16.0f)),
            .flex_shrink = 0.0f,
            .gap = ui::px(16.0f),
        },
        {0.09f, 0.09f, 0.1f, 1.0f}
    );
    const auto right = spawn_panel(
        commands,
        columns,
        ui::Node {
            .width = ui::px(338.0f),
            .padding = ui::all(ui::px(16.0f)),
            .flex_shrink = 0.0f,
            .gap = ui::px(12.0f),
        },
        {0.09f, 0.09f, 0.1f, 1.0f}
    );

    spawn_text(commands, left, "Core controls", font, 18.0f, DIM_TEXT);

    const auto button = spawn_panel(
        commands,
        left,
        ui::Node {
            .width = ui::px(150.0f),
            .height = ui::px(65.0f),
            .border = ui::all(ui::px(5.0f)),
            .padding = ui::axes(ui::px(20.0f), ui::px(16.0f)),
            .flex_shrink = 0.0f,
        },
        NORMAL_BUTTON
    );
    commands.entity(button).add(
        ui_widgets::Button {},
        DemoButton {},
        input_focus::TabIndex {},
        ui::Text {.value = "Button"},
        text::TextFont {.font = font, .font_size = 28.0f},
        text::TextLayout {.justify = text::Justify::Center},
        text::TextColor {.color = TEXT_COLOR},
        ui::BorderColor::all(BLACK),
        ui::BorderRadius::all(ui::percent(50.0f))
    );

    const auto tooltip_content = spawn_panel(
        commands,
        root,
        ui::Node {
            .display = ui::Display::None,
            .position_type = ui::PositionType::Absolute,
            .width = ui::px(220.0f),
            .height = ui::px(32.0f),
            .padding = ui::axes(ui::px(10.0f), ui::px(7.0f)),
        },
        {0.04f, 0.04f, 0.04f, 0.98f}
    );
    commands.entity(tooltip_content)
        .add(ui::ZIndex {.value = 200}, ui::BorderRadius::all(ui::px(4.0f)));
    spawn_text(
        commands,
        tooltip_content,
        "This button also has a tooltip",
        font,
        14.0f
    );
    commands.entity(button).add(
        ui_widgets::Tooltip {.content = tooltip_content, .delay = 0.45f}
    );

    const auto slider = spawn_panel(
        commands,
        left,
        ui::Node {
            .width = ui::px(270.0f),
            .height = ui::px(16.0f),
            .flex_shrink = 0.0f,
        },
        SLIDER_TRACK
    );
    commands.entity(slider).add(
        ui_widgets::Slider {.track_click = ui_widgets::TrackClick::Snap},
        ui_widgets::SliderValue {.value = 50.0f},
        ui_widgets::SliderRange::new_range(0.0f, 100.0f),
        ui_widgets::SliderStep {.value = 5.0f},
        input_focus::TabIndex {},
        ui::BorderRadius::all(ui::px(8.0f))
    );
    const auto slider_thumb = spawn_panel(
        commands,
        slider,
        ui::Node {
            .position_type = ui::PositionType::Absolute,
            .width = ui::px(16.0f),
            .height = ui::px(16.0f),
            .left = ui::px(0.0f),
            .top = ui::px(0.0f),
        },
        PRESSED_BUTTON
    );
    commands.entity(slider_thumb)
        .add(
            ui_widgets::SliderThumb {},
            DemoSliderThumb {},
            ui::BorderRadius::all(ui::percent(50.0f))
        );

    auto checkbox_row_commands = commands.spawn();
    checkbox_row_commands
        .add(
            ui::Node {
                .height = ui::px(34.0f),
                .flex_direction = ui::FlexDirection::Row,
                .align_items = ui::AlignItems::Center,
                .flex_shrink = 0.0f,
                .gap = ui::px(10.0f),
            }
        )
        .set_parent(left);
    const auto checkbox_row = checkbox_row_commands.id();
    const auto checkbox = spawn_panel(
        commands,
        checkbox_row,
        ui::Node {
            .width = ui::px(28.0f),
            .height = ui::px(28.0f),
            .border = ui::all(ui::px(3.0f)),
            .flex_shrink = 0.0f,
        },
        NORMAL_BUTTON
    );
    commands.entity(checkbox).add(
        ui_widgets::Checkbox {},
        ui::Checked {},
        input_focus::TabIndex {},
        ui::BorderColor::all(BLACK),
        ui::BorderRadius::all(ui::px(5.0f))
    );
    spawn_text(commands, checkbox_row, "Checkbox", font, 18.0f);

    auto radio_commands = commands.spawn();
    radio_commands
        .add(
            ui::Node {
                .height = ui::px(38.0f),
                .flex_direction = ui::FlexDirection::Row,
                .align_items = ui::AlignItems::Center,
                .flex_shrink = 0.0f,
                .gap = ui::px(12.0f),
            },
            ui_widgets::RadioGroup {},
            input_focus::TabIndex {}
        )
        .set_parent(left);
    const auto radio_group = radio_commands.id();
    for (int index = 0; index < 3; ++index) {
        const auto radio = spawn_panel(
            commands,
            radio_group,
            ui::Node {
                .width = ui::px(28.0f),
                .height = ui::px(28.0f),
                .border = ui::all(ui::px(3.0f)),
                .flex_shrink = 0.0f,
            },
            NORMAL_BUTTON
        );
        commands.entity(radio).add(
            ui_widgets::RadioButton {},
            ui::BorderColor::all(BLACK),
            ui::BorderRadius::all(ui::percent(50.0f))
        );
        if (index == 0) {
            commands.entity(radio).add(ui::Checked {});
        }
    }
    spawn_text(commands, radio_group, "Radio", font, 18.0f);

    const auto input = spawn_panel(
        commands,
        left,
        ui::Node {
            .width = ui::px(270.0f),
            .height = ui::px(44.0f),
            .border = ui::all(ui::px(3.0f)),
            .padding = ui::axes(ui::px(10.0f), ui::px(8.0f)),
            .flex_shrink = 0.0f,
        },
        NORMAL_BUTTON
    );
    commands.entity(input).add(
        ui_widgets::TextInput {},
        ui_widgets::SelectAllOnFocus {},
        ui::Text {.value = "Edit me"},
        text::TextFont {.font = font, .font_size = 18.0f},
        text::TextLayout {.line_break = text::LineBreak::NoWrap},
        text::TextColor {.color = TEXT_COLOR},
        input_focus::TabIndex {},
        ui::BorderColor::all(BLACK),
        ui::BorderRadius::all(ui::px(6.0f))
    );
    add_text_input_decorations(commands, input);

    spawn_text(commands, right, "Composite controls", font, 18.0f, DIM_TEXT);

    const auto menu_popup = spawn_panel(
        commands,
        root,
        ui::Node {
            .display = ui::Display::None,
            .position_type = ui::PositionType::Absolute,
            .width = ui::px(220.0f),
            .height = ui::px(118.0f),
            .padding = ui::all(ui::px(6.0f)),
            .gap = ui::px(4.0f),
        },
        NORMAL_BUTTON
    );
    commands.entity(menu_popup)
        .add(
            ui_widgets::MenuPopup {},
            ui::ZIndex {.value = 100},
            ui::BorderColor::all(BLACK),
            ui::BorderRadius::all(ui::px(5.0f))
        );
    for (const std::string_view label : {"New", "Open", "Save"}) {
        const auto item = spawn_panel(
            commands,
            menu_popup,
            ui::Node {
                .height = ui::px(32.0f),
                .padding = ui::axes(ui::px(10.0f), ui::px(6.0f)),
                .flex_shrink = 0.0f,
            },
            NORMAL_BUTTON
        );
        commands.entity(item).add(
            ui_widgets::MenuItem {},
            DemoMenuItem {},
            ui::Text {.value = std::string(label)},
            text::TextFont {.font = font, .font_size = 16.0f},
            text::TextColor {.color = TEXT_COLOR},
            ui::BorderRadius::all(ui::px(4.0f))
        );
    }
    const auto menu_button = spawn_panel(
        commands,
        right,
        ui::Node {
            .width = ui::px(220.0f),
            .height = ui::px(52.0f),
            .border = ui::all(ui::px(5.0f)),
            .padding = ui::axes(ui::px(16.0f), ui::px(11.0f)),
            .flex_shrink = 0.0f,
        },
        NORMAL_BUTTON
    );
    commands.entity(menu_button)
        .add(
            ui_widgets::MenuButton {.popup = menu_popup},
            DemoButton {},
            ui::Text {.value = "Menu       v"},
            text::TextFont {.font = font, .font_size = 22.0f},
            text::TextColor {.color = TEXT_COLOR},
            ui::BorderColor::all(BLACK),
            ui::BorderRadius::all(ui::px(5.0f))
        );
    commands.entity(menu_popup)
        .add(
            ui_widgets::Popover {
                .anchor = menu_button,
                .placement = {
                    .side = ui_widgets::PopoverSide::Bottom,
                    .gap = 4.0f,
                },
            }
        );

    const auto list_box = spawn_panel(
        commands,
        right,
        ui::Node {
            .width = ui::px(270.0f),
            .height = ui::px(112.0f),
            .padding = ui::all(ui::px(5.0f)),
            .flex_shrink = 0.0f,
            .gap = ui::px(4.0f),
        },
        SLIDER_TRACK
    );
    commands.entity(list_box).add(
        ui_widgets::ListBox {},
        ui::BorderRadius::all(ui::px(5.0f))
    );
    for (const std::string_view label :
         {"List item A", "List item B", "List item C"}) {
        spawn_list_item(commands, list_box, std::string(label), font);
    }

    const auto select_popup = spawn_panel(
        commands,
        root,
        ui::Node {
            .display = ui::Display::None,
            .position_type = ui::PositionType::Absolute,
            .width = ui::px(220.0f),
            .height = ui::px(118.0f),
            .padding = ui::all(ui::px(5.0f)),
            .gap = ui::px(4.0f),
        },
        SLIDER_TRACK
    );
    commands.entity(select_popup).add(ui::ZIndex {.value = 100});
    auto select_list_commands = commands.spawn();
    select_list_commands
        .add(ui::Node {.gap = ui::px(4.0f)}, ui_widgets::ListBox {})
        .set_parent(select_popup);
    const auto select_list = select_list_commands.id();
    for (const std::string_view label : {"Low", "Medium", "High"}) {
        spawn_list_item(commands, select_list, std::string(label), font);
    }
    const auto select = spawn_panel(
        commands,
        right,
        ui::Node {
            .width = ui::px(220.0f),
            .height = ui::px(44.0f),
            .border = ui::all(ui::px(4.0f)),
            .padding = ui::axes(ui::px(12.0f), ui::px(8.0f)),
            .flex_shrink = 0.0f,
        },
        NORMAL_BUTTON
    );
    commands.entity(select).add(
        ui_widgets::Select {
            .popup = select_popup,
            .list_box = select_list,
        },
        DemoButton {},
        ui::Text {.value = "Select..."},
        text::TextFont {.font = font, .font_size = 18.0f},
        text::TextColor {.color = TEXT_COLOR},
        ui::BorderColor::all(BLACK),
        ui::BorderRadius::all(ui::px(5.0f))
    );
    commands.entity(select_popup)
        .add(
            ui_widgets::Popover {.anchor = select, .placement = {.gap = 4.0f}}
        );

    const auto combo_popup = spawn_panel(
        commands,
        root,
        ui::Node {
            .display = ui::Display::None,
            .position_type = ui::PositionType::Absolute,
            .width = ui::px(270.0f),
            .height = ui::px(118.0f),
            .padding = ui::all(ui::px(5.0f)),
            .gap = ui::px(4.0f),
        },
        SLIDER_TRACK
    );
    commands.entity(combo_popup).add(ui::ZIndex {.value = 100});
    auto combo_list_commands = commands.spawn();
    combo_list_commands
        .add(ui::Node {.gap = ui::px(4.0f)}, ui_widgets::ListBox {})
        .set_parent(combo_popup);
    const auto combo_list = combo_list_commands.id();
    for (const std::string_view label : {"Warrior", "Mage", "Ranger"}) {
        spawn_list_item(commands, combo_list, std::string(label), font);
    }
    const auto combo = spawn_panel(
        commands,
        right,
        ui::Node {
            .width = ui::px(270.0f),
            .height = ui::px(44.0f),
            .border = ui::all(ui::px(3.0f)),
            .padding = ui::axes(ui::px(10.0f), ui::px(8.0f)),
            .flex_shrink = 0.0f,
        },
        NORMAL_BUTTON
    );
    commands.entity(combo).add(
        ui_widgets::ComboBox {
            .popup = combo_popup,
            .list_box = combo_list,
        },
        ui::Text {.value = "Type or choose..."},
        text::TextFont {.font = font, .font_size = 17.0f},
        text::TextLayout {.line_break = text::LineBreak::NoWrap},
        text::TextColor {.color = TEXT_COLOR},
        ui::BorderColor::all(BLACK),
        ui::BorderRadius::all(ui::px(5.0f))
    );
    commands.entity(combo_popup)
        .add(ui_widgets::Popover {.anchor = combo, .placement = {.gap = 4.0f}});
    add_text_input_decorations(commands, combo);

    spawn_text(
        commands,
        card,
        "Press D to toggle disabled states  |  Tab / arrows / Enter / Esc",
        font,
        14.0f,
        DIM_TEXT,
        ui::Node {.height = ui::px(20.0f), .flex_shrink = 0.0f}
    );
}

void update_button_styles(
    Query<
        Entity,
        const ui::Interaction,
        ui::BackgroundColor,
        ui::BorderColor,
        ui::Text>::Filter<With<DemoButton>> buttons,
    Query<Entity, const ui::Pressed> pressed,
    Query<Entity, const ui::InteractionDisabled> disabled,
    ResRO<input_focus::InputFocus> focus
) {
    for (auto [entity, interaction, background, border, label] : buttons) {
        const bool is_disabled = disabled.get(entity).has_value();
        const bool is_pressed = pressed.get(entity).has_value();
        background->color = is_disabled ? NORMAL_BUTTON :
                            is_pressed && interaction != ui::Interaction::None ?
                                          PRESSED_BUTTON :
                            interaction == ui::Interaction::Hovered ?
                                          HOVERED_BUTTON :
                                          NORMAL_BUTTON;
        border = ui::BorderColor::all(
            is_disabled                                        ? DISABLED :
            is_pressed && interaction != ui::Interaction::None ? RED :
            interaction == ui::Interaction::Hovered            ? WHITE :
            focus->get() == Optional<Entity> {entity} ? PRESSED_BUTTON :
                                                        BLACK
        );
        const auto& current_label = label.read().value;
        if (current_label == "Button" || current_label == "Hover" ||
            current_label == "Press" || current_label == "Disabled") {
            auto next_label = label.read();
            next_label.value = is_disabled ? "Disabled" :
                               is_pressed  ? "Press" :
                               interaction == ui::Interaction::Hovered ?
                                            "Hover" :
                                            "Button";
            if (next_label.value != current_label) {
                label = std::move(next_label);
            }
        }
    }
}

void update_toggle_styles(
    Query<Entity, const ui::Interaction, ui::BackgroundColor, ui::BorderColor>::
        Filter<Or<With<ui_widgets::Checkbox>, With<ui_widgets::RadioButton>>>
            toggles,
    Query<Entity, const ui::Checked> checked,
    Query<Entity, const ui::InteractionDisabled> disabled
) {
    for (auto [entity, interaction, background, border] : toggles) {
        const bool is_checked = checked.get(entity).has_value();
        const bool is_disabled = disabled.get(entity).has_value();
        background->color =
            is_disabled                             ? NORMAL_BUTTON :
            interaction == ui::Interaction::Pressed ? PRESSED_BUTTON :
            is_checked                              ? PRESSED_BUTTON :
            interaction == ui::Interaction::Hovered ? HOVERED_BUTTON :
                                                      NORMAL_BUTTON;
        border = ui::BorderColor::all(
            is_disabled                             ? DISABLED :
            interaction == ui::Interaction::Pressed ? RED :
            interaction == ui::Interaction::Hovered ? WHITE :
                                                      BLACK
        );
    }
}

void update_menu_item_styles(
    Query<Entity, const ui::Interaction, ui::BackgroundColor>::Filter<
        With<DemoMenuItem>> items,
    Query<Entity, const ui::Pressed> pressed,
    Query<Entity, const ui::InteractionDisabled> disabled
) {
    for (auto [entity, interaction, background] : items) {
        background->color = disabled.get(entity) ? NORMAL_BUTTON :
                            pressed.get(entity)  ? PRESSED_BUTTON :
                            interaction == ui::Interaction::Hovered ?
                                                  HOVERED_BUTTON :
                                                  NORMAL_BUTTON;
    }
}

void update_list_item_styles(
    Query<Entity, const ui::Interaction, ui::BackgroundColor>::Filter<
        With<DemoListItem>> items,
    Query<Entity, const ui::Pressed> pressed,
    Query<Entity, const ui::Selected> selected,
    Query<Entity, const ui::InteractionDisabled> disabled
) {
    for (auto [entity, interaction, background] : items) {
        background->color =
            disabled.get(entity)                        ? NORMAL_BUTTON :
            pressed.get(entity) || selected.get(entity) ? PRESSED_BUTTON :
            interaction == ui::Interaction::Hovered     ? HOVERED_BUTTON :
                                                          NORMAL_BUTTON;
    }
}

void update_slider_thumb(
    Query<
        Entity,
        const DemoSliderThumb,
        ui::Node,
        ui::BackgroundColor,
        const ui::ComputedNode> thumbs,
    Query<Entity, const ChildOf> parents,
    Query<
        Entity,
        const ui_widgets::SliderValue,
        const ui_widgets::SliderRange,
        const ui::Interaction,
        const ui::ComputedNode> sliders
) {
    for (auto [entity, marker, node, background, computed] : thumbs) {
        (void)marker;
        const auto parent = parents.get(entity);
        if (!parent) {
            continue;
        }
        const auto slider = sliders.get(std::get<1>(*parent).parent);
        if (!slider) {
            continue;
        }
        const auto& value = std::get<1>(*slider);
        const auto& range = std::get<2>(*slider);
        const auto interaction = std::get<3>(*slider);
        const auto& slider_node = std::get<4>(*slider);
        const float travel =
            std::max(0.0f, slider_node.size.x - computed.size.x);
        node->left = ui::px(travel * range.thumb_position(value.value));
        background->color = interaction == ui::Interaction::None ?
                                PRESSED_BUTTON :
                                Color4F {0.5f, 0.9f, 0.5f, 1.0f};
    }
}

void update_input_styles(
    Query<Entity, ui::BorderColor>::Filter<With<ui_widgets::TextInput>> inputs,
    Query<Entity, const ui::InteractionDisabled> disabled,
    ResRO<input_focus::InputFocus> focus
) {
    for (auto [entity, border] : inputs) {
        border = ui::BorderColor::all(
            disabled.get(entity)                      ? DISABLED :
            focus->get() == Optional<Entity> {entity} ? WHITE :
                                                        BLACK
        );
    }
}

void apply_selection_labels(
    EventReader<ui_widgets::SelectionChange> changes,
    Query<Entity, ui::Text> labels,
    Query<Entity, const OptionText> options,
    Query<Entity, text::EditableText> editable
) {
    while (const auto change = changes.next()) {
        const auto option = options.get(change->option);
        if (!option) {
            continue;
        }
        const auto& value = std::get<1>(*option).value;
        if (const auto input = editable.get(change->source)) {
            auto editor = std::get<1>(*input);
            editor->queue(
                text::TextEdit {.kind = text::TextEditKind::SelectAll}
            );
            editor->queue(text::TextEdit::insert(value));
        } else if (auto label = labels.get(change->source)) {
            auto text = std::get<1>(*label).read();
            text.value = value;
            std::get<1>(*label) = std::move(text);
        }
    }
}

void toggle_disabled(
    Query<Entity>::Filter<
        Or<With<DemoButton>,
           With<ui_widgets::Slider>,
           With<ui_widgets::Checkbox>,
           With<ui_widgets::RadioButton>,
           With<ui_widgets::TextInput>,
           With<ui_widgets::MenuItem>,
           With<ui_widgets::ListItem>>> controls,
    Query<Entity, const ui::InteractionDisabled> disabled,
    ResRO<KeyInput> keyboard,
    Commands commands
) {
    if (!keyboard->just_pressed(KeyCode::D)) {
        return;
    }
    for (const auto& [entity] : controls) {
        if (disabled.get(entity)) {
            commands.entity(entity).remove<ui::InteractionDisabled>();
        } else {
            commands.entity(entity).add(ui::InteractionDisabled {});
        }
    }
}

void report_events(
    EventReader<ui_widgets::Activate> activated,
    EventReader<ui_widgets::MenuEvent> menu_events,
    EventReader<ui_widgets::TextSubmit> submitted
) {
    while (const auto event = activated.next()) {
        info("Standard widget {} activated", event->entity);
    }
    while (const auto event = menu_events.next()) {
        if (event->action == ui_widgets::MenuAction::Activated) {
            info("Menu item {} activated", event->source);
        }
    }
    while (const auto event = submitted.next()) {
        info("Text input {} submitted: {}", event->entity, event->value);
    }
}

} // namespace

int main(int argc, char** argv) {
    const auto options = parse_arguments(argc, argv);
    App app;
    app.add_resource(
        WindowConfig {
            .width = 900,
            .height = 760,
            .title = "Fei Standard Widgets",
        }
    );
    app.add_plugin<OpenGLGlfwPlugin>()
        .add_plugin<SpritePlugin>()
        .add_plugin<ui::UiPlugin>()
        .add_plugin<ui_widgets::ButtonPlugin>()
        .add_plugin<ui_widgets::CheckboxPlugin>()
        .add_plugin<ui_widgets::RadioGroupPlugin>()
        .add_plugin<ui_widgets::SliderPlugin>()
        .add_plugin<ui_widgets::TextInputPlugin>()
        .add_plugin<ui_widgets::PopoverPlugin>()
        .add_plugin<ui_widgets::MenuPlugin>()
        .add_plugin<ui_widgets::ListBoxPlugin>()
        .add_plugin<ui_widgets::SelectPlugin>()
        .add_plugin<ui_widgets::TooltipPlugin>()
        .add_plugin<ui::rendering::UiRenderingPlugin>()
        .add_systems(PreStartUp, setup)
        .add_systems(
            Update,
            chain(
                ui_widgets::checkbox_self_update,
                ui_widgets::radio_self_update,
                ui_widgets::slider_self_update,
                ui_widgets::list_box_self_update,
                apply_selection_labels,
                update_button_styles,
                update_toggle_styles,
                update_menu_item_styles,
                update_list_item_styles,
                update_slider_thumb,
                update_input_styles,
                toggle_disabled
            ),
            report_events
        );

    if (options.devtools) {
        app.add_plugin<ReflectionPlugin>()
            .add_plugin(devtools::CorePlugin {})
            .add_plugin(devtools::input::ProviderPlugin {})
            .add_plugin(devtools::rendering::ProviderPlugin {});
    }
    app.run();
}
