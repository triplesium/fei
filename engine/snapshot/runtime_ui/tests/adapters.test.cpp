#include "snapshot_runtime_ui/adapters.hpp"

#include "app/app.hpp"
#include "app/reflection_plugin.hpp"
#include "asset/assets.hpp"
#include "asset/server.hpp"
#include "core/image.hpp"
#include "ecs/event.hpp"
#include "ecs/hierarchy.hpp"
#include "input_focus/focus.hpp"
#include "snapshot_runtime/adapters.hpp"
#include "snapshot_runtime_asset/adapters.hpp"
#include "text/editable.hpp"
#include "text/text.hpp"
#include "ui/image.hpp"
#include "ui/interaction.hpp"
#include "ui/measurement.hpp"
#include "ui/node.hpp"
#include "ui/surface.hpp"
#include "ui/text.hpp"
#include "ui_widgets/checkbox.hpp"
#include "ui_widgets/menu.hpp"
#include "ui_widgets/plugin.hpp"
#include "ui_widgets/popover.hpp"
#include "ui_widgets/scroll_area.hpp"
#include "ui_widgets/scrollbar.hpp"
#include "ui_widgets/select.hpp"
#include "ui_widgets/slider.hpp"
#include "ui_widgets/text_input.hpp"
#include "ui_widgets/value_change.hpp"
#include "window/window.hpp"

#include <algorithm>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <string>
#include <vector>

using namespace ets;

namespace {

struct ComplexUiWorld {
    App app;
    snapshot::CheckpointStore checkpoints;
    std::vector<Entity> entities;
    Entity root;
    Entity scroll_area;
    Entity content;
    Entity checkbox;
    Entity slider;
    Entity slider_thumb;
    Entity text_input;
    Entity caret;
    Entity image;
    Entity popup;
    Entity menu_button;

    ComplexUiWorld() {
        app.add_resource(
               Window {
                   .width = 640,
                   .height = 480,
               }
        )
            .add_plugin<ReflectionPlugin>()
            .add_plugin<ui_widgets::CheckboxPlugin>()
            .add_plugin<ui_widgets::SliderPlugin>()
            .add_plugin<ui_widgets::ScrollAreaPlugin>()
            .add_plugin<ui_widgets::ScrollbarPlugin>()
            .add_plugin<ui_widgets::MenuPlugin>()
            .add_plugin<ui_widgets::SelectPlugin>();
        app.finish();

        auto& world = app.world();
        root = make_entity();
        scroll_area = make_entity(root);
        content = make_entity(scroll_area);
        checkbox = make_entity(content);
        slider = make_entity(content);
        slider_thumb = make_entity(slider);
        text_input = make_entity(content);
        caret = make_entity(text_input);
        image = make_entity(content);
        popup = make_entity(root);
        menu_button = make_entity(content);

        world.add_component(
            root,
            ui::Node {
                .overflow = ui::Overflow::hidden(),
                .width = ui::px(400.0f),
                .height = ui::px(300.0f),
            }
        );
        world.add_component(
            scroll_area,
            ui::Node {
                .overflow = ui::Overflow::scroll_y(),
                .width = ui::px(320.0f),
                .height = ui::px(120.0f),
            }
        );
        world.add_component(scroll_area, ui_widgets::ScrollArea {});
        world.add_component(
            content,
            ui::Node {
                .height = ui::px(360.0f),
                .flex_shrink = 0.0f,
                .gap = ui::px(8.0f),
            }
        );

        world.add_component(
            checkbox,
            ui::Node {.height = ui::px(30.0f), .flex_shrink = 0.0f}
        );
        world.add_component(checkbox, ui_widgets::Checkbox {});
        world.add_component(checkbox, ui::Checked {});

        world.add_component(
            slider,
            ui::Node {
                .width = ui::px(220.0f),
                .height = ui::px(24.0f),
                .flex_shrink = 0.0f,
            }
        );
        world.add_component(slider, ui_widgets::Slider {});
        world.add_component(slider, ui_widgets::SliderValue {.value = 0.75f});
        world.add_component(
            slider,
            ui_widgets::SliderRange::new_range(0.0f, 1.0f)
        );
        world.add_component(
            slider_thumb,
            ui::Node {.width = ui::px(16.0f), .height = ui::px(24.0f)}
        );
        world.add_component(slider_thumb, ui_widgets::SliderThumb {});

        world.add_component(
            text_input,
            ui::Node {.height = ui::px(32.0f), .flex_shrink = 0.0f}
        );
        world.add_component(text_input, ui_widgets::TextInput {});
        world.add_component(text_input, ui::Text {.value = "checkpoint text"});
        world.add_component(
            text_input,
            text::EditableText {.anchor = 4, .cursor = 10}
        );
        world.add_component(caret, ui_widgets::TextCaret {});

        const auto image_handle = app.resource<AssetServer>().load<Image>(
            "project://spot_texture.png"
        );
        world.add_component(
            image,
            ui::Node {
                .width = ui::px(48.0f),
                .height = ui::px(48.0f),
                .flex_shrink = 0.0f,
            }
        );
        world.add_component(image, ui::ImageNode {.image = image_handle});

        world.add_component(
            popup,
            ui::Node {
                .position_type = ui::PositionType::Absolute,
                .width = ui::px(140.0f),
                .height = ui::px(80.0f),
            }
        );
        world.add_component(popup, ui_widgets::MenuPopup {});
        world.add_component(popup, ui_widgets::Popover {.anchor = checkbox});
        world.add_component(popup, ui::ZIndex {.value = 20});

        world.add_component(
            menu_button,
            ui::Node {.height = ui::px(30.0f), .flex_shrink = 0.0f}
        );
        world.add_component(
            menu_button,
            ui_widgets::MenuButton {.popup = popup}
        );
        world.add_component(menu_button, ui_widgets::MenuOpen {});
        world.add_component(menu_button, ui_widgets::Expanded {});

        app.run_schedule(PostUpdate);
        app.run_schedule(PostUpdate);
        world.get_component_rw<ui::ScrollPosition>(scroll_area).write().offset =
            {0.0f, 90.0f};
        app.run_schedule(PostUpdate);

        world.resource<input_focus::InputFocus>().set(
            text_input,
            input_focus::FocusCause::Programmatic
        );
        world.resource<input_focus::InputFocus>().notified_entity = text_input;
        world.resource<Events<ui_widgets::ValueChange<std::string>>>().send(
            ui_widgets::ValueChange<std::string> {
                .source = text_input,
                .value = "checkpoint event",
                .is_final = false,
            }
        );

        world.add_component(checkbox, ui::Interaction::Pressed);
        world.add_component(checkbox, ui::Pressed {});
        world.add_component(
            scroll_area,
            ui::RelativeCursorPosition {
                .cursor_over = true,
                .normalized = Vector2 {0.5f, 0.5f},
            }
        );
        world.add_component(
            slider,
            ui_widgets::SliderDragState {
                .dragging = true,
                .changed = true,
                .offset = 0.2f,
                .pointer_start = 10.0f,
                .pointer_position = 20.0f,
            }
        );
        world.add_component(
            text_input,
            ui_widgets::TextInputState {
                .changed = true,
                .pointer_dragging = true,
                .pointer_moved = true,
                .select_all_on_release = true,
            }
        );

        auto& registry = checkpoints.registry();
        registry.resource<AppStates>(snapshot::ResourcePolicy::Ignore);
        registry.resource<CommandsQueue>(snapshot::ResourcePolicy::Ignore);
        REQUIRE(snapshot_runtime::configure_builtin_adapters(world, registry));
        REQUIRE(
            snapshot_runtime_asset::configure_asset_adapters(world, registry)
        );
        REQUIRE(snapshot_runtime_ui::configure_ui_adapters(world, registry));
    }

    Entity make_entity(Optional<Entity> parent = nullopt) {
        auto& world = app.world();
        const auto entity = world.entity();
        entities.push_back(entity);
        if (parent) {
            world.set_parent(entity, *parent);
        }
        return entity;
    }

    Entity restored_entity(
        const snapshot::RestoreResult& restored,
        Entity original
    ) const {
        auto sorted = entities;
        std::ranges::sort(sorted);
        const auto found = std::ranges::find(sorted, original);
        REQUIRE(found != sorted.end());
        return restored.entity(
            static_cast<snapshot::SnapshotEntityId>(
                std::distance(sorted.begin(), found) + 1
            )
        );
    }
};

const snapshot::SnapshotAuditEntry*
component_audit(const snapshot::SnapshotAudit& audit, TypeId type) {
    const auto found =
        std::ranges::find(audit.components, type, [](const auto& entry) {
            return entry.type;
        });
    return found == audit.components.end() ? nullptr : &*found;
}

} // namespace

TEST_CASE(
    "UI snapshot restores semantic state and rebuilds complex derived state",
    "[snapshot][ui][restore]"
) {
    ComplexUiWorld test;
    auto& world = test.app.world();

    const auto coverage = snapshot::audit(world, test.checkpoints.registry());
    std::string diagnostics = coverage.runtime_message;
    for (const auto& entry : coverage.components) {
        if (entry.disposition == snapshot::AuditDisposition::Unregistered ||
            (!entry.serializable &&
             entry.disposition == snapshot::AuditDisposition::Snapshot)) {
            diagnostics +=
                "\ncomponent " + entry.type_name + ": " + entry.message;
        }
    }
    for (const auto& entry : coverage.resources) {
        if (entry.disposition == snapshot::AuditDisposition::Unregistered ||
            (!entry.serializable &&
             entry.disposition == snapshot::AuditDisposition::Snapshot)) {
            diagnostics +=
                "\nresource " + entry.type_name + ": " + entry.message;
        }
    }
    if (!coverage.ready) {
        auto probe = snapshot::capture(world, test.checkpoints.registry());
        if (!probe) {
            diagnostics += "\ncapture " + probe.error().path + ": " +
                           probe.error().message;
        }
    }
    INFO(diagnostics);
    REQUIRE(coverage.ready);
    REQUIRE(coverage.complete);
    const auto* computed =
        component_audit(coverage, type_id<ui::ComputedNode>());
    const auto* content = component_audit(coverage, type_id<ui::ContentSize>());
    const auto* text_layout =
        component_audit(coverage, type_id<text::TextLayoutInfo>());
    REQUIRE(computed);
    REQUIRE(content);
    REQUIRE(text_layout);
    CHECK(computed->disposition == snapshot::AuditDisposition::Rebuild);
    CHECK(content->disposition == snapshot::AuditDisposition::Rebuild);
    CHECK(text_layout->disposition == snapshot::AuditDisposition::Rebuild);
    REQUIRE(test.checkpoints.create("complex-ui", world, true));

    const auto image_path = world.resource<AssetServer>().asset_path(
        AssetServer::asset_key(
            world.get_component<ui::ImageNode>(test.image).image
        )
    );
    REQUIRE(image_path);
    world.get_component_rw<ui_widgets::SliderValue>(test.slider).write().value =
        0.1f;
    world.remove_component<ui::Checked>(test.checkbox);
    world.get_component_rw<ui::Text>(test.text_input).write().value = "mutated";
    world.get_component_rw<text::EditableText>(test.text_input).write().cursor =
        0;
    world.get_component_rw<ui::ScrollPosition>(test.scroll_area)
        .write()
        .offset = Vector2::Zero;
    world.resource<input_focus::InputFocus>().set(test.checkbox);
    world.remove_component<ui_widgets::Expanded>(test.menu_button);
    world.remove_component<ui_widgets::MenuOpen>(test.menu_button);
    world.get_component_rw<ui::ComputedNode>(test.root).write().size = {
        9999.0f,
        9999.0f
    };
    world.resource<ui::Surface>().clear();
    world.resource<ui::Stack>().nodes.clear();
    world.resource<Events<ui_widgets::ValueChange<std::string>>>().clear();

    auto restored = test.checkpoints.restore("complex-ui", world);
    REQUIRE(restored);
    const auto root = test.restored_entity(*restored, test.root);
    const auto scroll_area = test.restored_entity(*restored, test.scroll_area);
    const auto checkbox = test.restored_entity(*restored, test.checkbox);
    const auto slider = test.restored_entity(*restored, test.slider);
    const auto text_input = test.restored_entity(*restored, test.text_input);
    const auto image = test.restored_entity(*restored, test.image);
    const auto popup = test.restored_entity(*restored, test.popup);
    const auto menu_button = test.restored_entity(*restored, test.menu_button);

    CHECK(
        world.get_component<ui_widgets::SliderValue>(slider).value ==
        Catch::Approx(0.75f)
    );
    CHECK(world.has_component<ui::Checked>(checkbox));
    CHECK(world.get_component<ui::Text>(text_input).value == "checkpoint text");
    CHECK(world.get_component<text::EditableText>(text_input).anchor == 4);
    CHECK(world.get_component<text::EditableText>(text_input).cursor == 10);
    CHECK(
        world.get_component<ui::ScrollPosition>(scroll_area).offset.y ==
        Catch::Approx(90.0f)
    );
    CHECK(world.has_component<ui_widgets::Expanded>(menu_button));
    CHECK(world.has_component<ui_widgets::MenuOpen>(menu_button));
    const auto restored_image = world.get_component<ui::ImageNode>(image).image;
    REQUIRE(restored_image);
    const auto restored_image_path = world.resource<AssetServer>().asset_path(
        AssetServer::asset_key(restored_image)
    );
    REQUIRE(restored_image_path);
    CHECK(*restored_image_path == *image_path);
    REQUIRE(world.resource<Assets<Image>>().get(restored_image));
    REQUIRE(world.resource<input_focus::InputFocus>().get());
    CHECK(*world.resource<input_focus::InputFocus>().get() == text_input);
    REQUIRE(world.resource<input_focus::InputFocus>().notified_entity);
    CHECK(
        *world.resource<input_focus::InputFocus>().notified_entity == text_input
    );

    CHECK(world.resource<ui::Surface>().contains(root));
    CHECK(world.resource<ui::LayoutState>().initialized);
    CHECK_FALSE(world.resource<ui::Stack>().nodes.empty());
    CHECK(
        world.get_component<ui::ComputedNode>(root).size ==
        Vector2 {400.0f, 300.0f}
    );
    CHECK(world.has_component<ui::CalculatedClip>(scroll_area));
    CHECK(world.has_component<ui::ContentSize>(text_input));
    CHECK(world.has_component<text::TextLayoutInfo>(text_input));
    CHECK(world.has_component<ui::TextNodeFlags>(text_input));
    CHECK(
        world.get_component<ui::ComputedStackIndex>(popup).value >
        world.get_component<ui::ComputedStackIndex>(checkbox).value
    );

    CHECK(
        world.get_component<ui::Interaction>(checkbox) == ui::Interaction::None
    );
    CHECK_FALSE(world.has_component<ui::Pressed>(checkbox));
    CHECK_FALSE(
        world.get_component<ui::RelativeCursorPosition>(scroll_area).cursor_over
    );
    CHECK_FALSE(
        world.get_component<ui_widgets::SliderDragState>(slider).dragging
    );
    CHECK_FALSE(world.get_component<ui_widgets::TextInputState>(text_input)
                    .pointer_dragging);

    const auto& events =
        world.resource<Events<ui_widgets::ValueChange<std::string>>>();
    const auto event = events.get_event(0);
    REQUIRE(event);
    CHECK(event->event.source == text_input);
    CHECK(event->event.value == "checkpoint event");
    CHECK_FALSE(event->event.is_final);
}

TEST_CASE(
    "UI snapshot V1 rejects custom ContentSize that cannot be rebuilt",
    "[snapshot][ui][boundary]"
) {
    ComplexUiWorld test;
    auto& world = test.app.world();
    const auto custom = test.make_entity(test.root);
    world.add_component(custom, ui::Node {});
    world.add_component(custom, ui::ContentSize::fixed({12.0f, 34.0f}));

    const auto coverage = snapshot::audit(world, test.checkpoints.registry());
    CHECK_FALSE(coverage.ready);
    CHECK_FALSE(coverage.runtime_ready);
    CHECK(
        coverage.runtime_message.find("only rebuilds ContentSize") !=
        std::string::npos
    );
    auto created = test.checkpoints.create("unsupported-ui", world, false);
    REQUIRE_FALSE(created);
    CHECK(
        created.error().kind ==
        snapshot::SnapshotError::Kind::CheckpointBoundaryFailed
    );
}
