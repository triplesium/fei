#include "app/app.hpp"
#include "asset/assets.hpp"
#include "asset/server.hpp"
#include "base/log.hpp"
#include "core/image.hpp"
#include "core/plugin.hpp"
#include "core/text.hpp"
#include "core/time.hpp"
#include "core/transform.hpp"
#include "ecs/commands.hpp"
#include "ecs/query.hpp"
#include "ecs/system_config.hpp"
#include "ecs/system_params.hpp"
#include "ecs/system_set.hpp"
#include "graphics_webgpu_browser/plugin.hpp"
#include "input/input.hpp"
#include "input_focus/tab_navigation.hpp"
#include "rendering/plugin.hpp"
#include "rendering/render_app.hpp"
#include "sprite/components.hpp"
#include "sprite/plugin.hpp"
#include "sprite/renderer.hpp"
#include "text/font.hpp"
#include "text/text.hpp"
#include "ui/plugin.hpp"
#include "ui_rendering/plugin.hpp"
#include "ui_rendering/renderer.hpp"
#include "ui_widgets/button.hpp"
#include "ui_widgets/plugin.hpp"
#include "ui_widgets/scroll_area.hpp"
#include "ui_widgets/text_input.hpp"
#include "window/window.hpp"
#include "window_browser/input.hpp"

#include <algorithm>
#include <cmath>
#include <emscripten.h>
#include <string>
#include <string_view>

namespace fei::browser_sample {
namespace {

struct BrowserSprite {};

struct BrowserUiButton {};
struct BrowserUiTextInput {};
struct BrowserUiScrollArea {};

struct BrowserPresentation {
    bool sprite_presented {false};
    bool text_presented {false};
};

struct BrowserSmokeSystems {
    struct Report : SystemSet<Report> {};
};

struct BrowserUiState {
    int clicks {0};
    int published_clicks {-1};
    std::string published_text;
    float published_scroll {-1.0F};
    Vector2 published_viewport;
    int published_interaction {-1};
    int published_focus {-2};
    bool published {false};
};

void set_browser_status(const char* status) {
    EM_ASM(
        {
            const status = UTF8ToString($0);
            document.documentElement.dataset.feiStatus = status;
            console.log("[fei] " + status);
        },
        status
    );
}

void set_browser_input_status(const char* status, float sprite_x) {
    EM_ASM(
        {
            document.documentElement.dataset.feiInput = UTF8ToString($0);
            document.documentElement.dataset.feiSpriteX = $1.toString();
        },
        status,
        sprite_x
    );
}

void publish_browser_text_status(int glyph_count, int glyph_batch_count) {
    EM_ASM(
        {
            const root = document.documentElement.dataset;
            root.feiTextStatus = "ready";
            root.feiFontAtlasUploaded = "true";
            root.feiGlyphCount = $0.toString();
            root.feiGlyphBatches = $1.toString();
            root.feiFramePresented = "true";
        },
        glyph_count,
        glyph_batch_count
    );
}

void publish_browser_ui_status(
    int clicks,
    const char* text,
    float scroll_y,
    int interaction,
    int focus,
    const Window& window,
    const ui::ComputedNode& button,
    const ui::ComputedNode& input,
    const ui::ComputedNode& scroll
) {
    EM_ASM(
        {
            const root = document.documentElement.dataset;
            root.feiUiStatus = "ready";
            root.feiUiClicks = $0.toString();
            root.feiUiText = UTF8ToString($1);
            root.feiUiScrollY = $2.toString();
            root.feiUiInteraction = $3.toString();
            root.feiUiViewport = $4 + "," + $5;
            root.feiUiFocus = $6.toString();
        },
        clicks,
        text,
        scroll_y,
        interaction,
        window.width,
        window.height,
        focus
    );
    EM_ASM(
        {
            const root = document.documentElement.dataset;
            root.feiUiButtonRect = $0 + "," + $1 + "," + $2 + "," + $3;
            root.feiUiInputRect = $4 + "," + $5 + "," + $6 + "," + $7;
            root.feiUiScrollRect = $8 + "," + $9 + "," + $10 + "," + $11;
        },
        button.position.x,
        button.position.y,
        button.size.x,
        button.size.y,
        input.position.x,
        input.position.y,
        input.size.x,
        input.size.y,
        scroll.position.x,
        scroll.position.y,
        scroll.size.x,
        scroll.size.y
    );
}

Entity spawn_ui_panel(
    Commands& commands,
    Entity parent,
    ui::Node node,
    Color4F color
) {
    auto entity = commands.spawn();
    entity.add(node, ui::BackgroundColor {.color = color}).set_parent(parent);
    return entity.id();
}

void add_text_input_decorations(Commands& commands, Entity input) {
    const auto selection = spawn_ui_panel(
        commands,
        input,
        ui::Node {
            .display = ui::Display::None,
            .position_type = ui::PositionType::Absolute,
        },
        {0.25F, 0.55F, 0.95F, 0.38F}
    );
    commands.entity(selection).add(ui_widgets::TextSelection {});
    const auto caret = spawn_ui_panel(
        commands,
        input,
        ui::Node {
            .display = ui::Display::None,
            .position_type = ui::PositionType::Absolute,
        },
        {0.95F, 0.78F, 0.3F, 1.0F}
    );
    commands.entity(caret).add(ui_widgets::TextCaret {});
}

void setup_browser_scene(
    ResRW<AssetServer> asset_server,
    ResRO<Assets<TextAsset>> text_assets,
    Commands commands
) {
    set_browser_status("loading browser assets");
    const auto ready_asset = asset_server->load<TextAsset>("browser/ready.txt");
    const auto ready_text = text_assets->get(ready_asset);
    if (!ready_text || !std::string_view(ready_text->text())
                            .starts_with("fei browser assets ready")) {
        set_browser_status("browser asset loading failed");
        error("Browser readiness asset was unavailable or invalid");
        return;
    }

    const auto image = asset_server->load<Image>("browser/checker.ppm");
    commands.spawn().add(
        Camera2d {
            .vertical_size = 5.0F,
            .clear_color = {0.08F, 0.12F, 0.2F, 1.0F},
        },
        Transform2d {}
    );
    commands.spawn().add(
        Sprite {
            .image = image,
            .size = {2.5F, 2.5F},
        },
        Transform2d {},
        BrowserSprite {}
    );
    set_browser_status("sprite scene loaded from VFS");
}

void setup_browser_ui(ResRW<AssetServer> asset_server, Commands commands) {
    const auto font =
        asset_server->load<text::Font>("browser-fonts/Cousine-Regular.ttf");

    auto root_commands = commands.spawn();
    root_commands.add(
        ui::Node {
            .padding = ui::all(ui::px(24.0F)),
            .align_items = ui::AlignItems::Start,
        },
        input_focus::TabGroup {}
    );
    const auto root = root_commands.id();

    const auto card = spawn_ui_panel(
        commands,
        root,
        ui::Node {
            .width = ui::px(360.0F),
            .height = ui::px(430.0F),
            .padding = ui::all(ui::px(18.0F)),
            .flex_shrink = 0.0F,
            .gap = ui::px(12.0F),
        },
        {0.055F, 0.075F, 0.12F, 0.96F}
    );
    commands.entity(card).add(
        ui::BorderRadius::all(ui::px(12.0F)),
        ui::BorderColor::all({0.2F, 0.45F, 0.8F, 1.0F})
    );

    auto title = commands.spawn();
    title
        .add(
            ui::Node {.height = ui::px(28.0F), .flex_shrink = 0.0F},
            ui::Text {.value = "Browser UI"},
            text::TextFont {.font = font, .font_size = 22.0F},
            text::TextColor {.color = {0.92F, 0.96F, 1.0F, 1.0F}}
        )
        .set_parent(card);

    const auto button = spawn_ui_panel(
        commands,
        card,
        ui::Node {
            .width = ui::px(180.0F),
            .height = ui::px(48.0F),
            .border = ui::all(ui::px(2.0F)),
            .padding = ui::axes(ui::px(12.0F), ui::px(10.0F)),
            .flex_shrink = 0.0F,
        },
        {0.15F, 0.35F, 0.68F, 1.0F}
    );
    commands.entity(button).add(
        BrowserUiButton {},
        ui_widgets::Button {},
        input_focus::TabIndex {.index = 0},
        ui::Text {.value = "Click me"},
        text::TextFont {.font = font, .font_size = 18.0F},
        text::TextLayout {.justify = text::Justify::Center},
        text::TextColor {.color = {1.0F, 1.0F, 1.0F, 1.0F}},
        ui::BorderColor::all({0.35F, 0.65F, 1.0F, 1.0F}),
        ui::BorderRadius::all(ui::px(7.0F))
    );

    const auto input = spawn_ui_panel(
        commands,
        card,
        ui::Node {
            .width = ui::px(300.0F),
            .height = ui::px(44.0F),
            .border = ui::all(ui::px(2.0F)),
            .padding = ui::axes(ui::px(10.0F), ui::px(8.0F)),
            .flex_shrink = 0.0F,
        },
        {0.09F, 0.13F, 0.2F, 1.0F}
    );
    commands.entity(input).add(
        BrowserUiTextInput {},
        ui_widgets::TextInput {},
        ui_widgets::SelectAllOnFocus {},
        ui::Text {.value = "Edit me"},
        text::TextFont {.font = font, .font_size = 18.0F},
        text::TextLayout {.line_break = text::LineBreak::NoWrap},
        text::TextColor {.color = {0.92F, 0.96F, 1.0F, 1.0F}},
        input_focus::TabIndex {.index = 1},
        ui::BorderColor::all({0.28F, 0.45F, 0.7F, 1.0F}),
        ui::BorderRadius::all(ui::px(6.0F))
    );
    add_text_input_decorations(commands, input);

    const auto scroll = spawn_ui_panel(
        commands,
        card,
        ui::Node {
            .overflow = ui::Overflow::scroll_y(),
            .width = ui::px(300.0F),
            .height = ui::px(220.0F),
            .padding = ui::all(ui::px(8.0F)),
            .flex_shrink = 0.0F,
            .gap = ui::px(6.0F),
        },
        {0.07F, 0.11F, 0.17F, 1.0F}
    );
    commands.entity(scroll).add(
        BrowserUiScrollArea {},
        ui_widgets::ScrollArea {},
        ui::BorderRadius::all(ui::px(7.0F))
    );
    for (int index = 1; index <= 8; ++index) {
        const auto row = spawn_ui_panel(
            commands,
            scroll,
            ui::Node {
                .height = ui::px(40.0F),
                .padding = ui::axes(ui::px(10.0F), ui::px(8.0F)),
                .flex_shrink = 0.0F,
            },
            index % 2 == 0 ? Color4F {0.1F, 0.2F, 0.32F, 1.0F} :
                             Color4F {0.09F, 0.16F, 0.26F, 1.0F}
        );
        commands.entity(row).add(
            ui::Text {.value = "Scrollable row " + std::to_string(index)},
            text::TextFont {.font = font, .font_size = 15.0F},
            text::TextColor {.color = {0.82F, 0.9F, 1.0F, 1.0F}},
            ui::BorderRadius::all(ui::px(4.0F))
        );
    }
}

void update_browser_ui_button_style(
    Query<const ui::Interaction, ui::BackgroundColor>::Filter<
        With<BrowserUiButton>> buttons
) {
    for (auto [interaction, background] : buttons) {
        background->color = interaction == ui::Interaction::Pressed ?
                                Color4F {0.35F, 0.7F, 0.95F, 1.0F} :
                            interaction == ui::Interaction::Hovered ?
                                Color4F {0.22F, 0.5F, 0.85F, 1.0F} :
                                Color4F {0.15F, 0.35F, 0.68F, 1.0F};
    }
}

void report_browser_ui(
    EventReader<ui_widgets::Activate> activations,
    Query<Entity>::Filter<With<BrowserUiButton>> button_entities,
    Query<const ui::Interaction, const ui::ComputedNode>::Filter<
        With<BrowserUiButton>> buttons,
    Query<Entity, const ui::Text, const ui::ComputedNode>::Filter<
        With<BrowserUiTextInput>> inputs,
    Query<const ui::ScrollPosition, const ui::ComputedNode>::Filter<
        With<BrowserUiScrollArea>> scroll_areas,
    ResRO<input_focus::InputFocus> focus,
    ResRO<Window> window,
    ResRW<BrowserUiState> state
) {
    while (const auto activation = activations.next()) {
        if (button_entities.get(activation->entity)) {
            ++state->clicks;
        }
    }

    const ui::Interaction* interaction = nullptr;
    const ui::ComputedNode* button_node = nullptr;
    for (const auto [current_interaction, computed] : buttons) {
        interaction = &current_interaction;
        button_node = &computed;
        break;
    }
    const ui::Text* text = nullptr;
    const ui::ComputedNode* input_node = nullptr;
    Optional<Entity> input_entity;
    for (const auto [entity, current_text, computed] : inputs) {
        input_entity = entity;
        text = &current_text;
        input_node = &computed;
        break;
    }
    const ui::ScrollPosition* scroll_position = nullptr;
    const ui::ComputedNode* scroll_node = nullptr;
    for (const auto [position, computed] : scroll_areas) {
        scroll_position = &position;
        scroll_node = &computed;
        break;
    }
    if (interaction == nullptr || button_node == nullptr || text == nullptr ||
        input_node == nullptr || scroll_position == nullptr ||
        scroll_node == nullptr) {
        return;
    }

    const auto interaction_value = static_cast<int>(*interaction);
    int focus_value = -1;
    if (const auto focused = focus->get()) {
        focus_value = button_entities.get(*focused) ? 0 :
                      input_entity == focused       ? 1 :
                                                      2;
    }
    const auto viewport = Vector2 {
        static_cast<float>(window->width),
        static_cast<float>(window->height),
    };
    if (state->published && state->published_clicks == state->clicks &&
        state->published_text == text->value &&
        state->published_scroll == scroll_position->offset.y &&
        state->published_viewport == viewport &&
        state->published_interaction == interaction_value &&
        state->published_focus == focus_value) {
        return;
    }

    publish_browser_ui_status(
        state->clicks,
        text->value.c_str(),
        scroll_position->offset.y,
        interaction_value,
        focus_value,
        *window,
        *button_node,
        *input_node,
        *scroll_node
    );
    state->published = true;
    state->published_clicks = state->clicks;
    state->published_text = text->value;
    state->published_scroll = scroll_position->offset.y;
    state->published_viewport = viewport;
    state->published_interaction = interaction_value;
    state->published_focus = focus_value;
}

void animate_browser_sprite(
    Query<Transform2d>::Filter<With<BrowserSprite>> query,
    ResRO<Time> time,
    ResRO<KeyInput> keys,
    ResRO<MouseInput> mouse,
    ResRO<MouseScrollInput> scroll
) {
    for (auto [transform] : query) {
        float direction = 0.0F;
        if (keys->pressed(KeyCode::Left) || keys->pressed(KeyCode::A)) {
            direction -= 1.0F;
        }
        if (keys->pressed(KeyCode::Right) || keys->pressed(KeyCode::D)) {
            direction += 1.0F;
        }
        transform->position.x = std::clamp(
            transform->position.x + direction * time->delta() * 1.5F,
            -2.5F,
            2.5F
        );
        transform->position.y = std::sin(time->elapsed_time()) * 0.2F;
        transform->rotation = time->elapsed_time() * 35.0F;

        if (keys->just_pressed(KeyCode::Right)) {
            set_browser_input_status("Right:pressed", transform->position.x);
        }
        if (keys->just_released(KeyCode::Right)) {
            set_browser_input_status("Right:released", transform->position.x);
        }
        if (keys->just_pressed(KeyCode::Tab)) {
            set_browser_input_status("Tab:pressed", transform->position.x);
        }
        if (keys->just_pressed(KeyCode::Enter)) {
            set_browser_input_status("Enter:pressed", transform->position.x);
        }
        if (mouse->just_pressed(MouseButton::Left)) {
            set_browser_input_status(
                "MouseLeft:pressed",
                transform->position.x
            );
        }
        if (mouse->just_released(MouseButton::Left)) {
            set_browser_input_status(
                "MouseLeft:released",
                transform->position.x
            );
        }
        if (scroll->delta() != Vector2::Zero) {
            set_browser_input_status("Wheel", transform->position.x);
        }
    }
}

void report_browser_render_smoke(
    ResRO<SpritePhase> sprite_phase,
    ResRO<ui::rendering::Phase> ui_phase,
    ResRW<BrowserPresentation> presentation
) {
    if (!presentation->sprite_presented && sprite_phase->active &&
        !sprite_phase->batches.empty()) {
        presentation->sprite_presented = true;
        set_browser_status("sprite pipeline presented");
    }
    if (!presentation->text_presented && ui_phase->active &&
        ui_phase->glyph_count > 0 && ui_phase->glyph_batch_count > 0) {
        presentation->text_presented = true;
        publish_browser_text_status(
            static_cast<int>(ui_phase->glyph_count),
            static_cast<int>(ui_phase->glyph_batch_count)
        );
        set_browser_status("ui text presented");
    }
}

class BrowserSamplePlugin final : public Plugin {
  public:
    void dependencies(PluginDependencies& dependencies) const override {
        dependencies.require<WebGpuBrowserPlugin>()
            .require<BrowserInputPlugin>()
            .require<CorePlugin>()
            .require<SpritePlugin>()
            .require<ui_widgets::ButtonPlugin>()
            .require<ui_widgets::ScrollAreaPlugin>()
            .require<ui_widgets::TextInputPlugin>()
            .require<ui::rendering::UiRenderingPlugin>();
    }

    void setup(App& app) override {
        app.add_resource(BrowserUiState {})
            .add_systems(PreStartUp, setup_browser_scene, setup_browser_ui)
            .add_systems(
                Update,
                animate_browser_sprite,
                update_browser_ui_button_style
            )
            .add_systems(Last, report_browser_ui);
        app.sub_app<RenderApp>()
            .add_resource(BrowserPresentation {})
            .configure_sets(
                RenderLast,
                BrowserSmokeSystems::Report {}
                    .after<RenderingSystems::Present>()
            )
            .add_systems(
                RenderLast,
                report_browser_render_smoke |
                    in_set<BrowserSmokeSystems::Report>() | main_thread()
            );
    }
};

} // namespace
} // namespace fei::browser_sample

int main() {
    using namespace fei;
    using namespace fei::browser_sample;

    App app;
    app.add_plugin<BrowserSamplePlugin>();
    app.run();
    return 0;
}
