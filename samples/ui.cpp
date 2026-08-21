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
#include "text/font.hpp"
#include "text/text.hpp"
#include "ui/image.hpp"
#include "ui/plugin.hpp"
#include "ui_rendering/plugin.hpp"
#include "ui_widgets/button.hpp"
#include "ui_widgets/checkbox.hpp"
#include "ui_widgets/list_box.hpp"
#include "ui_widgets/menu.hpp"
#include "ui_widgets/plugin.hpp"
#include "ui_widgets/popover.hpp"
#include "ui_widgets/radio.hpp"
#include "ui_widgets/scroll_area.hpp"
#include "ui_widgets/scrollbar.hpp"
#include "ui_widgets/select.hpp"
#include "ui_widgets/slider.hpp"
#include "ui_widgets/text_input.hpp"
#include "ui_widgets/tooltip.hpp"
#include "window/window.hpp"
#include "window_glfw/window.hpp"

#include <algorithm>
#include <string_view>
#include <tuple>

using namespace fei;

namespace {

struct Options {
    bool devtools {false};
};

Options parse_arguments(int argc, char** argv) {
    Options result;
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument {argv[index]};
        if (argument == "--devtools") {
            result.devtools = true;
            continue;
        }
        fatal("Unknown sample-ui argument {}", argument);
    }
    return result;
}

Entity
spawn_panel(Commands& commands, Entity parent, ui::Node node, Color4F color) {
    auto panel = commands.spawn();
    panel.add(node, ui::BackgroundColor {.color = color}).set_parent(parent);
    return panel.id();
}

void setup(
    Commands commands,
    ResRW<AssetServer> asset_server,
    ResRW<input_focus::InputFocus> input_focus,
    ResRW<input_focus::InputFocusVisible> input_focus_visible,
    EventWriter<ui_widgets::SetChecked> set_checked
) {
    asset_server->emplace_source<FilesystemAssetSource>(
        "sample-ui-fonts",
        FEI_ASSETS_PATH "/../engine/imgui/fonts"
    );
    const auto font =
        asset_server->load<text::Font>("sample-ui-fonts://Cousine-Regular.ttf");
    commands.spawn().add(
        Camera2d {
            .clear_color = {0.025f, 0.035f, 0.055f, 1.0f},
        },
        Transform2d {}
    );

    auto root_commands = commands.spawn();
    root_commands.add(
        ui::Node {
            .padding = ui::all(ui::px(16.0f)),
            .gap = ui::px(12.0f),
        },
        ui::BackgroundColor {.color = {0.055f, 0.07f, 0.11f, 1.0f}},
        input_focus::TabGroup {}
    );
    const auto root = root_commands.id();

    const auto header = spawn_panel(
        commands,
        root,
        ui::Node {.height = ui::px(72.0f)},
        {0.12f, 0.2f, 0.34f, 1.0f}
    );

    auto content_commands = commands.spawn();
    content_commands
        .add(
            ui::Node {
                .flex_direction = ui::FlexDirection::Row,
                .flex_grow = 1.0f,
                .gap = ui::px(12.0f),
            }
        )
        .set_parent(root);
    const auto content = content_commands.id();

    const auto scroll_area = spawn_panel(
        commands,
        content,
        ui::Node {
            .overflow = ui::Overflow::scroll_y(),
            .width = ui::percent(25.0f),
            .padding = ui::all(ui::px(10.0f)),
            .gap = ui::px(8.0f),
        },
        {0.1f, 0.28f, 0.25f, 1.0f}
    );
    commands.entity(scroll_area).add(ui_widgets::ScrollArea {});
    for (int index = 0; index < 9; ++index) {
        const float shade = static_cast<float>(index) / 8.0f;
        spawn_panel(
            commands,
            scroll_area,
            ui::Node {
                .height = ui::px(48.0f),
                .flex_shrink = 0.0f,
            },
            {0.12f + shade * 0.18f,
             0.32f + shade * 0.18f,
             0.28f + shade * 0.12f,
             1.0f}
        );
    }
    const auto scrollbar = spawn_panel(
        commands,
        content,
        ui::Node {
            .width = ui::px(12.0f),
            .flex_shrink = 0.0f,
        },
        {0.08f, 0.1f, 0.15f, 1.0f}
    );
    commands.entity(scrollbar).add(
        ui_widgets::Scrollbar::new_scrollbar(
            scroll_area,
            ui_widgets::ControlOrientation::Vertical,
            28.0f
        )
    );
    const auto scrollbar_thumb = spawn_panel(
        commands,
        scrollbar,
        ui::Node {.width = ui::px(12.0f)},
        {0.25f, 0.75f, 0.7f, 1.0f}
    );
    commands.entity(scrollbar_thumb)
        .add(
            ui_widgets::ScrollbarThumb {},
            ui::BorderRadius::all(ui::px(6.0f))
        );
    const auto main = spawn_panel(
        commands,
        content,
        ui::Node {
            .overflow = ui::Overflow::clip(),
            .border = ui::all(ui::px(4.0f)),
            .padding = ui::all(ui::px(24.0f)),
            .flex_grow = 1.0f,
        },
        {0.16f, 0.12f, 0.25f, 1.0f}
    );
    commands.entity(main).add(
        ui::BorderColor {
            .left = {0.25f, 0.75f, 0.7f, 1.0f},
            .top = {0.45f, 0.35f, 0.9f, 1.0f},
            .right = {0.9f, 0.35f, 0.55f, 1.0f},
            .bottom = {0.95f, 0.7f, 0.25f, 1.0f},
        },
        ui::BorderRadius::all(ui::px(24.0f))
    );
    auto image = commands.spawn();
    image
        .add(
            ui::Node {
                .position_type = ui::PositionType::Absolute,
                .width = ui::px(180.0f),
                .height = ui::px(180.0f),
                .left = ui::px(24.0f),
                .top = ui::px(24.0f),
            },
            ui::ImageNode {
                .image = asset_server->load<Image>("awesomeface.png"),
                .color = {0.9f, 1.0f, 0.95f, 1.0f},
            }
        )
        .set_parent(main);
    auto label = commands.spawn();
    label
        .add(
            ui::Node {
                .position_type = ui::PositionType::Absolute,
                .width = ui::px(380.0f),
                .left = ui::px(232.0f),
                .top = ui::px(38.0f),
            },
            ui::Text {
                .value = "Bevy-style measured text wraps with Flex layout.",
            },
            text::TextFont {.font = font, .font_size = 28.0f},
            text::TextLayout {
                .justify = text::Justify::Center,
                .line_break = text::LineBreak::WordOrCharacter,
            },
            text::TextColor {.color = {0.9f, 0.92f, 1.0f, 1.0f}}
        )
        .set_parent(main);
    const auto button = spawn_panel(
        commands,
        main,
        ui::Node {
            .position_type = ui::PositionType::Absolute,
            .width = ui::percent(45.0f),
            .height = ui::px(120.0f),
            .border = ui::all(ui::px(4.0f)),
            .right = ui::px(-80.0f),
            .bottom = ui::px(24.0f),
        },
        {0.72f, 0.24f, 0.31f, 0.9f}
    );
    commands.entity(button).add(
        ui_widgets::Button {},
        ui::RelativeCursorPosition {},
        input_focus::TabIndex {},
        ui::BorderColor::all({0.0f, 0.0f, 0.0f, 0.0f})
    );

    const auto disabled_button = spawn_panel(
        commands,
        main,
        ui::Node {
            .position_type = ui::PositionType::Absolute,
            .width = ui::percent(28.0f),
            .height = ui::px(72.0f),
            .border = ui::all(ui::px(4.0f)),
            .left = ui::px(232.0f),
            .bottom = ui::px(24.0f),
        },
        {0.28f, 0.3f, 0.36f, 1.0f}
    );
    commands.entity(disabled_button)
        .add(
            ui_widgets::Button {},
            ui::InteractionDisabled {},
            input_focus::TabIndex {.index = 1},
            ui::BorderColor::all({0.0f, 0.0f, 0.0f, 0.0f})
        );

    const auto checkbox = spawn_panel(
        commands,
        main,
        ui::Node {
            .position_type = ui::PositionType::Absolute,
            .width = ui::px(52.0f),
            .height = ui::px(52.0f),
            .border = ui::all(ui::px(4.0f)),
            .left = ui::px(232.0f),
            .bottom = ui::px(112.0f),
        },
        {0.2f, 0.65f, 0.45f, 1.0f}
    );
    commands.entity(checkbox).add(
        ui_widgets::Checkbox {},
        input_focus::TabIndex {.index = 2},
        ui::BorderColor::all({0.0f, 0.0f, 0.0f, 0.0f})
    );
    set_checked.send(
        ui_widgets::SetChecked {.entity = checkbox, .checked = true}
    );

    auto radio_group_commands = commands.spawn();
    radio_group_commands
        .add(
            ui::Node {
                .position_type = ui::PositionType::Absolute,
                .width = ui::px(118.0f),
                .height = ui::px(44.0f),
                .border = ui::all(ui::px(3.0f)),
                .padding = ui::all(ui::px(4.0f)),
                .flex_direction = ui::FlexDirection::Row,
                .gap = ui::px(6.0f),
                .left = ui::px(300.0f),
                .top = ui::px(172.0f),
            },
            ui_widgets::RadioGroup {},
            input_focus::TabIndex {.index = 3},
            ui::BorderColor::all({0.0f, 0.0f, 0.0f, 0.0f}),
            ui::BorderRadius::all(ui::px(12.0f))
        )
        .set_parent(main);
    const auto radio_group = radio_group_commands.id();

    for (int index = 0; index < 3; ++index) {
        const auto radio = spawn_panel(
            commands,
            radio_group,
            ui::Node {
                .width = ui::px(28.0f),
                .height = ui::px(28.0f),
            },
            {0.14f, 0.16f, 0.22f, 1.0f}
        );
        commands.entity(radio).add(
            ui_widgets::RadioButton {},
            ui::BorderRadius::all(ui::percent(50.0f))
        );
        if (index == 0) {
            commands.entity(radio).add(ui::Checked {});
        }
    }

    const auto slider = spawn_panel(
        commands,
        main,
        ui::Node {
            .position_type = ui::PositionType::Absolute,
            .width = ui::px(180.0f),
            .height = ui::px(28.0f),
            .border = ui::all(ui::px(3.0f)),
            .left = ui::px(24.0f),
            .bottom = ui::px(24.0f),
        },
        {0.14f, 0.16f, 0.22f, 1.0f}
    );
    commands.entity(slider).add(
        ui_widgets::Slider {},
        ui_widgets::SliderValue {.value = 0.5f},
        ui_widgets::SliderRange::new_range(0.0f, 1.0f),
        ui_widgets::SliderStep {.value = 0.1f},
        input_focus::TabIndex {.index = 4},
        ui::BorderColor::all({0.0f, 0.0f, 0.0f, 0.0f}),
        ui::BorderRadius::all(ui::px(12.0f))
    );
    const auto thumb = spawn_panel(
        commands,
        slider,
        ui::Node {
            .position_type = ui::PositionType::Absolute,
            .width = ui::px(20.0f),
            .height = ui::px(20.0f),
            .left = ui::px(0.0f),
            .top = ui::px(1.0f),
        },
        {0.25f, 0.75f, 0.7f, 1.0f}
    );
    commands.entity(thumb).add(
        ui_widgets::SliderThumb {},
        ui::BorderRadius::all(ui::percent(50.0f))
    );
    const auto text_input = spawn_panel(
        commands,
        header,
        ui::Node {
            .position_type = ui::PositionType::Absolute,
            .width = ui::px(180.0f),
            .height = ui::px(44.0f),
            .border = ui::all(ui::px(3.0f)),
            .padding = ui::axes(ui::px(10.0f), ui::px(8.0f)),
            .left = ui::px(24.0f),
            .top = ui::px(14.0f),
        },
        {0.14f, 0.16f, 0.22f, 1.0f}
    );
    commands.entity(text_input)
        .add(
            ui_widgets::TextInput {},
            ui_widgets::SelectAllOnFocus {},
            ui::Text {.value = "Edit me"},
            text::TextFont {.font = font, .font_size = 20.0f},
            text::TextLayout {.line_break = text::LineBreak::NoWrap},
            text::TextColor {.color = {0.9f, 0.92f, 1.0f, 1.0f}},
            input_focus::TabIndex {.index = 5},
            ui::BorderColor::all({0.0f, 0.0f, 0.0f, 0.0f}),
            ui::BorderRadius::all(ui::px(8.0f))
        );
    const auto selection = spawn_panel(
        commands,
        text_input,
        ui::Node {
            .display = ui::Display::None,
            .position_type = ui::PositionType::Absolute,
        },
        {0.25f, 0.55f, 0.95f, 0.38f}
    );
    commands.entity(selection).add(ui_widgets::TextSelection {});
    const auto caret = spawn_panel(
        commands,
        text_input,
        ui::Node {
            .display = ui::Display::None,
            .position_type = ui::PositionType::Absolute,
        },
        {0.95f, 0.78f, 0.3f, 1.0f}
    );
    commands.entity(caret).add(ui_widgets::TextCaret {});

    const auto footer = spawn_panel(
        commands,
        root,
        ui::Node {
            .height = ui::px(64.0f),
            .padding = ui::all(ui::px(8.0f)),
            .flex_direction = ui::FlexDirection::Row,
            .flex_shrink = 0.0f,
            .gap = ui::px(12.0f),
        },
        {0.16f, 0.18f, 0.24f, 1.0f}
    );

    const auto menu_popup = spawn_panel(
        commands,
        root,
        ui::Node {
            .display = ui::Display::None,
            .position_type = ui::PositionType::Absolute,
            .width = ui::px(180.0f),
            .height = ui::px(124.0f),
            .padding = ui::all(ui::px(6.0f)),
            .gap = ui::px(4.0f),
        },
        {0.08f, 0.1f, 0.16f, 1.0f}
    );
    commands.entity(menu_popup)
        .add(
            ui_widgets::MenuPopup {},
            ui::ZIndex {.value = 100},
            ui::BorderRadius::all(ui::px(8.0f))
        );
    for (const std::string_view title : {"New Game", "Settings", "Quit"}) {
        const auto item = spawn_panel(
            commands,
            menu_popup,
            ui::Node {
                .height = ui::px(34.0f),
                .padding = ui::axes(ui::px(10.0f), ui::px(7.0f)),
                .flex_shrink = 0.0f,
            },
            {0.14f, 0.16f, 0.24f, 1.0f}
        );
        commands.entity(item).add(
            ui_widgets::MenuItem {},
            ui::Text {.value = std::string(title)},
            text::TextFont {.font = font, .font_size = 16.0f},
            text::TextColor {.color = {0.9f, 0.92f, 1.0f, 1.0f}},
            ui::BorderRadius::all(ui::px(5.0f))
        );
    }
    const auto menu_button = spawn_panel(
        commands,
        footer,
        ui::Node {
            .width = ui::px(150.0f),
            .height = ui::px(48.0f),
            .padding = ui::axes(ui::px(12.0f), ui::px(12.0f)),
            .flex_shrink = 0.0f,
        },
        {0.32f, 0.24f, 0.55f, 1.0f}
    );
    commands.entity(menu_button)
        .add(
            ui_widgets::MenuButton {.popup = menu_popup},
            ui::Text {.value = "Menu"},
            text::TextFont {.font = font, .font_size = 18.0f},
            text::TextColor {.color = {0.95f, 0.95f, 1.0f, 1.0f}},
            ui::BorderRadius::all(ui::px(8.0f))
        );
    commands.entity(menu_popup)
        .add(
            ui_widgets::Popover {
                .anchor = menu_button,
                .placement = {
                    .side = ui_widgets::PopoverSide::Top,
                    .align = ui_widgets::PopoverAlign::Start,
                    .gap = 6.0f,
                },
            }
        );

    const auto tooltip_content = spawn_panel(
        commands,
        root,
        ui::Node {
            .display = ui::Display::None,
            .position_type = ui::PositionType::Absolute,
            .width = ui::px(190.0f),
            .height = ui::px(32.0f),
            .padding = ui::axes(ui::px(8.0f), ui::px(6.0f)),
        },
        {0.04f, 0.05f, 0.08f, 0.98f}
    );
    commands.entity(tooltip_content)
        .add(
            ui::Text {.value = "Keyboard: arrows / Enter / Esc"},
            text::TextFont {.font = font, .font_size = 13.0f},
            text::TextColor {.color = {0.9f, 0.92f, 1.0f, 1.0f}},
            ui::ZIndex {.value = 110},
            ui::BorderRadius::all(ui::px(5.0f))
        );
    commands.entity(menu_button)
        .add(ui_widgets::Tooltip {.content = tooltip_content, .delay = 0.45f});

    const auto select_popup = spawn_panel(
        commands,
        root,
        ui::Node {
            .display = ui::Display::None,
            .position_type = ui::PositionType::Absolute,
            .width = ui::px(190.0f),
            .height = ui::px(124.0f),
            .padding = ui::all(ui::px(6.0f)),
            .gap = ui::px(4.0f),
        },
        {0.08f, 0.1f, 0.16f, 1.0f}
    );
    commands.entity(select_popup).add(ui::ZIndex {.value = 100});
    auto select_list_commands = commands.spawn();
    select_list_commands
        .add(ui::Node {.gap = ui::px(4.0f)}, ui_widgets::ListBox {})
        .set_parent(select_popup);
    const auto select_list = select_list_commands.id();
    for (const std::string_view title : {"Easy", "Normal", "Hard"}) {
        const auto item = spawn_panel(
            commands,
            select_list,
            ui::Node {
                .height = ui::px(34.0f),
                .padding = ui::axes(ui::px(10.0f), ui::px(7.0f)),
                .flex_shrink = 0.0f,
            },
            {0.14f, 0.16f, 0.24f, 1.0f}
        );
        commands.entity(item).add(
            ui_widgets::ListItem {},
            ui::Text {.value = std::string(title)},
            text::TextFont {.font = font, .font_size = 16.0f},
            text::TextColor {.color = {0.9f, 0.92f, 1.0f, 1.0f}}
        );
    }
    const auto select = spawn_panel(
        commands,
        footer,
        ui::Node {
            .width = ui::px(190.0f),
            .height = ui::px(48.0f),
            .padding = ui::axes(ui::px(12.0f), ui::px(12.0f)),
            .flex_shrink = 0.0f,
        },
        {0.12f, 0.35f, 0.38f, 1.0f}
    );
    commands.entity(select).add(
        ui_widgets::Select {
            .popup = select_popup,
            .list_box = select_list,
        },
        ui::Text {.value = "Difficulty"},
        text::TextFont {.font = font, .font_size = 18.0f},
        text::TextColor {.color = {0.95f, 0.95f, 1.0f, 1.0f}},
        ui::BorderRadius::all(ui::px(8.0f))
    );
    commands.entity(select_popup)
        .add(
            ui_widgets::Popover {
                .anchor = select,
                .placement = {
                    .side = ui_widgets::PopoverSide::Top,
                    .align = ui_widgets::PopoverAlign::Start,
                    .gap = 6.0f,
                },
            }
        );

    const auto combo_popup = spawn_panel(
        commands,
        root,
        ui::Node {
            .display = ui::Display::None,
            .position_type = ui::PositionType::Absolute,
            .width = ui::px(220.0f),
            .height = ui::px(124.0f),
            .padding = ui::all(ui::px(6.0f)),
            .gap = ui::px(4.0f),
        },
        {0.08f, 0.1f, 0.16f, 1.0f}
    );
    commands.entity(combo_popup).add(ui::ZIndex {.value = 100});
    auto combo_list_commands = commands.spawn();
    combo_list_commands
        .add(ui::Node {.gap = ui::px(4.0f)}, ui_widgets::ListBox {})
        .set_parent(combo_popup);
    const auto combo_list = combo_list_commands.id();
    for (const std::string_view title : {"Warrior", "Mage", "Ranger"}) {
        const auto item = spawn_panel(
            commands,
            combo_list,
            ui::Node {
                .height = ui::px(34.0f),
                .padding = ui::axes(ui::px(10.0f), ui::px(7.0f)),
                .flex_shrink = 0.0f,
            },
            {0.14f, 0.16f, 0.24f, 1.0f}
        );
        commands.entity(item).add(
            ui_widgets::ListItem {},
            ui::Text {.value = std::string(title)},
            text::TextFont {.font = font, .font_size = 16.0f},
            text::TextColor {.color = {0.9f, 0.92f, 1.0f, 1.0f}}
        );
    }
    const auto combo = spawn_panel(
        commands,
        footer,
        ui::Node {
            .width = ui::px(220.0f),
            .height = ui::px(48.0f),
            .border = ui::all(ui::px(2.0f)),
            .padding = ui::axes(ui::px(12.0f), ui::px(10.0f)),
            .flex_shrink = 0.0f,
        },
        {0.14f, 0.16f, 0.22f, 1.0f}
    );
    commands.entity(combo).add(
        ui_widgets::ComboBox {
            .popup = combo_popup,
            .list_box = combo_list,
        },
        ui::Text {.value = "Type or choose class"},
        text::TextFont {.font = font, .font_size = 16.0f},
        text::TextLayout {.line_break = text::LineBreak::NoWrap},
        text::TextColor {.color = {0.9f, 0.92f, 1.0f, 1.0f}},
        ui::BorderColor::all({0.3f, 0.65f, 0.75f, 1.0f}),
        ui::BorderRadius::all(ui::px(8.0f))
    );
    commands.entity(combo_popup)
        .add(
            ui_widgets::Popover {
                .anchor = combo,
                .placement = {
                    .side = ui_widgets::PopoverSide::Top,
                    .align = ui_widgets::PopoverAlign::Start,
                    .gap = 6.0f,
                },
            }
        );

    input_focus->set(button);
    input_focus_visible->visible = true;
}

void update_button_colors(
    Query<Entity, const ui::Interaction, ui::BackgroundColor>::
        Filter<Changed<ui::Interaction>, With<ui_widgets::Button>> buttons,
    Query<Entity, const ui::InteractionDisabled> disabled
) {
    for (auto [entity, interaction, background] : buttons) {
        background->color = disabled.get(entity) ?
                                Color4F {0.28f, 0.3f, 0.36f, 1.0f} :
                            interaction == ui::Interaction::Pressed ?
                                Color4F {0.95f, 0.7f, 0.25f, 1.0f} :
                            interaction == ui::Interaction::Hovered ?
                                Color4F {0.9f, 0.35f, 0.55f, 1.0f} :
                                Color4F {0.72f, 0.24f, 0.31f, 0.9f};
    }
}

void report_button_activations(EventReader<ui_widgets::Activate> events) {
    while (const auto event = events.next()) {
        info("UI button {} activated", event->entity);
    }
}

void report_text_submissions(EventReader<ui_widgets::TextSubmit> events) {
    while (const auto event = events.next()) {
        info("UI text input {} submitted: {}", event->entity, event->value);
    }
}

void report_selection_changes(EventReader<ui_widgets::SelectionChange> events) {
    while (const auto event = events.next()) {
        info("UI select {} chose option {}", event->source, event->option);
    }
}

void update_list_item_colors(
    Query<Entity, const ui::Interaction, ui::BackgroundColor>::Filter<
        With<ui_widgets::ListItem>> items,
    Query<Entity, const ui::Selected> selected
) {
    for (auto [entity, interaction, background] : items) {
        background->color = interaction == ui::Interaction::Pressed ?
                                Color4F {0.95f, 0.7f, 0.25f, 1.0f} :
                            interaction == ui::Interaction::Hovered ?
                                Color4F {0.28f, 0.3f, 0.48f, 1.0f} :
                            selected.get(entity) ?
                                Color4F {0.2f, 0.55f, 0.5f, 1.0f} :
                                Color4F {0.14f, 0.16f, 0.24f, 1.0f};
    }
}

void update_checkbox_colors(
    Query<Entity, const ui::Interaction, ui::BackgroundColor>::Filter<
        With<ui_widgets::Checkbox>> checkboxes,
    Query<Entity, const ui::Checked> checked
) {
    for (auto [entity, interaction, background] : checkboxes) {
        background->color = interaction == ui::Interaction::Pressed ?
                                Color4F {0.95f, 0.7f, 0.25f, 1.0f} :
                            checked.get(entity) ?
                                Color4F {0.2f, 0.65f, 0.45f, 1.0f} :
                                Color4F {0.14f, 0.16f, 0.22f, 1.0f};
    }
}

void update_radio_colors(
    Query<Entity, const ui::Interaction, ui::BackgroundColor>::Filter<
        With<ui_widgets::RadioButton>> radio_buttons,
    Query<Entity, const ui::Checked> checked
) {
    for (auto [entity, interaction, background] : radio_buttons) {
        background->color = interaction == ui::Interaction::Pressed ?
                                Color4F {0.95f, 0.7f, 0.25f, 1.0f} :
                            checked.get(entity) ?
                                Color4F {0.45f, 0.35f, 0.9f, 1.0f} :
                                Color4F {0.14f, 0.16f, 0.22f, 1.0f};
    }
}

void update_slider_thumb(
    Query<
        Entity,
        const ui_widgets::SliderThumb,
        ui::Node,
        const ui::ComputedNode> thumbs,
    Query<Entity, const ChildOf> parents,
    Query<
        Entity,
        const ui_widgets::SliderValue,
        const ui_widgets::SliderRange,
        const ui::ComputedNode> sliders
) {
    for (auto [entity, marker, node, computed] : thumbs) {
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
        const auto& slider_node = std::get<3>(*slider);
        const auto travel =
            std::max(0.0f, slider_node.size.x - computed.size.x);
        node->left = ui::px(travel * range.thumb_position(value.value));
    }
}

void update_focus_borders(
    Query<Entity, ui::BorderColor>::Filter<With<input_focus::TabIndex>>
        focusable,
    ResRO<input_focus::InputFocus> focus,
    ResRO<input_focus::InputFocusVisible> focus_visible
) {
    for (auto [entity, border] : focusable) {
        border = ui::BorderColor::all(
            input_focus::is_focus_visible(*focus, *focus_visible, entity) ?
                Color4F {1.0f, 0.8f, 0.25f, 1.0f} :
                Color4F {0.0f, 0.0f, 0.0f, 0.0f}
        );
    }
}

} // namespace

int main(int argc, char** argv) {
    const auto options = parse_arguments(argc, argv);
    App app;
    app.add_resource(
        GlfwWindowConfig {
            .width = 960,
            .height = 540,
            .title = "Fei UI Sample",
        }
    );
    app.add_plugin<OpenGLGlfwPlugin>()
        .add_plugin<SpritePlugin>()
        .add_plugin<ui::UiPlugin>()
        .add_plugin<ui_widgets::ButtonPlugin>()
        .add_plugin<ui_widgets::CheckboxPlugin>()
        .add_plugin<ui_widgets::RadioGroupPlugin>()
        .add_plugin<ui_widgets::SliderPlugin>()
        .add_plugin<ui_widgets::ScrollAreaPlugin>()
        .add_plugin<ui_widgets::ScrollbarPlugin>()
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
                update_checkbox_colors,
                update_radio_colors,
                update_list_item_colors,
                update_slider_thumb
            ),
            update_button_colors,
            update_focus_borders,
            report_button_activations,
            report_text_submissions,
            report_selection_changes
        );

    if (options.devtools) {
        app.add_plugin<ReflectionPlugin>()
            .add_plugin(devtools::CorePlugin {})
            .add_plugin(devtools::input::ProviderPlugin {})
            .add_plugin(devtools::rendering::ProviderPlugin {});
    }
    app.run();
}
