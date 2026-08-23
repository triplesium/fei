#include "snapshot_runtime_ui/adapters.hpp"

#include "asset/assets.hpp"
#include "asset/handle.hpp"
#include "core/image.hpp"
#include "ecs/archetype.hpp"
#include "ecs/event.hpp"
#include "ecs/world.hpp"
#include "input_focus/focus.hpp"
#include "refl/cls.hpp"
#include "refl/registry.hpp"
#include "snapshot/events.hpp"
#include "text/editable.hpp"
#include "text/font.hpp"
#include "text/pipeline.hpp"
#include "text/text.hpp"
#include "ui/image.hpp"
#include "ui/interaction.hpp"
#include "ui/measurement.hpp"
#include "ui/node.hpp"
#include "ui/plugin.hpp"
#include "ui/surface.hpp"
#include "ui/text.hpp"
#include "ui_widgets/button.hpp"
#include "ui_widgets/checkbox.hpp"
#include "ui_widgets/list_box.hpp"
#include "ui_widgets/menu.hpp"
#include "ui_widgets/plugin.hpp"
#include "ui_widgets/scroll_area.hpp"
#include "ui_widgets/scrollbar.hpp"
#include "ui_widgets/select.hpp"
#include "ui_widgets/slider.hpp"
#include "ui_widgets/text_input.hpp"
#include "ui_widgets/tooltip.hpp"
#include "ui_widgets/value_change.hpp"
#include "window/window.hpp"

#include <string>
#include <string_view>

namespace ets::snapshot_runtime_ui {
namespace {

snapshot::SnapshotError configuration_error(std::string message) {
    return snapshot::SnapshotError {
        .kind = snapshot::SnapshotError::Kind::InvalidConfiguration,
        .path = "ui",
        .message = std::move(message),
    };
}

snapshot::SnapshotError
unsupported_rebuild_source(Entity entity, std::string message) {
    return snapshot::SnapshotError {
        .kind = snapshot::SnapshotError::Kind::CheckpointBoundaryFailed,
        .path = "ui.entity[" + std::to_string(entity.value) + "]",
        .message = std::move(message),
    };
}

template<class T>
bool configure_event_if_present(
    World& world,
    snapshot::SnapshotRegistry& registry
) {
    if (!world.has_resource<Events<T>>()) {
        return true;
    }
    if (registry.codecs().find(type_id<Events<T>>()) != nullptr) {
        registry.resource<Events<T>>(snapshot::ResourcePolicy::Snapshot);
        return true;
    }
    return snapshot::register_event_resource<T>(registry);
}

template<class T>
void register_value_change_reflection(std::string_view local_name) {
    auto& registry = Registry::instance();
    if (registry.try_get_cls<ui_widgets::ValueChange<T>>()) {
        return;
    }
    registry
        .register_cls<ui_widgets::ValueChange<T>>(
            {"ets", "ui_widgets", "snapshot"},
            local_name
        )
        .add_property("source", &ui_widgets::ValueChange<T>::source)
        .add_property("value", &ui_widgets::ValueChange<T>::value)
        .add_property("is_final", &ui_widgets::ValueChange<T>::is_final);
}

Status<snapshot::SnapshotError> validate_rebuild_sources(const World& world) {
    for (const auto& [_, archetype] : world.archetypes()) {
        if (archetype.entities().empty()) {
            continue;
        }
        const auto entity = archetype.entities().front();
        const auto has_node = archetype.has_component(type_id<ui::Node>());
        const auto has_text = archetype.has_component(type_id<text::Text>());
        const auto has_image =
            archetype.has_component(type_id<ui::ImageNode>());
        if (archetype.has_component(type_id<ui::ContentSize>()) && !has_text &&
            !has_image) {
            return failure(unsupported_rebuild_source(
                entity,
                "UI snapshot V1 only rebuilds ContentSize owned by Text or "
                "ImageNode"
            ));
        }
        if ((archetype.has_component(type_id<ui::ComputedNode>()) ||
             archetype.has_component(type_id<ui::CalculatedClip>()) ||
             archetype.has_component(type_id<ui::ComputedStackIndex>())) &&
            !has_node) {
            return failure(unsupported_rebuild_source(
                entity,
                "Derived UI layout components require an authoritative Node"
            ));
        }
        if (archetype.has_component(type_id<ui::ImageNodeSize>()) &&
            !has_image) {
            return failure(unsupported_rebuild_source(
                entity,
                "ImageNodeSize requires an authoritative ImageNode"
            ));
        }
        if ((archetype.has_component(type_id<text::TextLayoutInfo>()) ||
             archetype.has_component(type_id<ui::TextNodeFlags>())) &&
            !has_text) {
            return failure(unsupported_rebuild_source(
                entity,
                "Derived text layout components require authoritative Text"
            ));
        }
    }
    return {};
}

void reset_transient_state(World& world) {
    world.run_system_once([](Query<ui::Interaction> interactions) {
        for (auto [interaction] : interactions) {
            interaction = ui::Interaction::None;
        }
    });
    world.run_system_once(
        [](Query<ui::RelativeCursorPosition> cursor_positions) {
            for (auto [position] : cursor_positions) {
                position = ui::RelativeCursorPosition {};
            }
        }
    );
    world.run_system_once([](Query<Entity>::Filter<With<ui::Pressed>> pressed,
                             Commands commands) {
        for (const auto& [entity] : pressed) {
            commands.entity(entity).remove<ui::Pressed>();
        }
    });
    world.run_system_once([](Query<ui_widgets::SliderDragState> states) {
        for (auto [state] : states) {
            state = ui_widgets::SliderDragState {};
        }
    });
    world.run_system_once([](Query<ui_widgets::ScrollbarDragState> states) {
        for (auto [state] : states) {
            state = ui_widgets::ScrollbarDragState {};
        }
    });
    world.run_system_once([](Query<ui_widgets::TextInputState> states) {
        for (auto [state] : states) {
            state = ui_widgets::TextInputState {};
        }
    });
    world.run_system_once(
        [](Query<Entity, const ui_widgets::Tooltip, ui_widgets::TooltipState>
               tooltips,
           Query<Entity, ui::Node> nodes) {
            for (auto [entity, tooltip, state] : tooltips) {
                (void)entity;
                state = ui_widgets::TooltipState {};
                if (const auto content = nodes.get(tooltip.content)) {
                    auto node = std::get<1>(*content);
                    auto next = node.read();
                    next.display = ui::Display::None;
                    node = next;
                }
            }
        }
    );
}

void synchronize_widget_shapes(World& world) {
    world.run_system_once(ui_widgets::sync_popovers);
    world.run_system_once(ui_widgets::sync_menus);
    world.run_system_once(ui_widgets::sync_list_boxes);
    world.run_system_once(ui_widgets::sync_selects);
    world.run_system_once(ui_widgets::sync_tooltips);
    world.run_system_once(ui_widgets::sync_buttons);
    world.run_system_once(ui_widgets::sync_checkboxes);
    world.run_system_once(ui_widgets::sync_radio_buttons);
    world.run_system_once(ui_widgets::sync_sliders);
    world.run_system_once(ui_widgets::sync_scroll_areas);
    world.run_system_once(ui_widgets::sync_scrollbars);
    world.run_system_once(ui_widgets::sync_text_inputs);
}

void rebuild_core_ui(World& world) {
    world.run_system_once(ui::sync_image_nodes);
    world.run_system_once(ui::sync_text_nodes);
    world.run_system_once(ui::sync_computed_nodes);
    world.run_system_once(ui::sync_computed_stack_indices);
    world.run_system_once(ui::invalidate_text_nodes);
    world.run_system_once(ui::update_image_content_sizes);
    world.run_system_once(ui::update_text_content_sizes);
    world.run_system_once(ui::compute_layout);
    world.run_system_once(ui::invalidate_text_layouts);
    world.run_system_once(ui::update_text_layouts);
    world.run_system_once(ui::update_clipping);
    world.run_system_once(ui::compute_stack);
}

void rebuild_positioned_widgets(World& world) {
    world.run_system_once(ui_widgets::update_popovers);
    world.run_system_once(ui::compute_layout);
    world.run_system_once(ui::invalidate_text_layouts);
    world.run_system_once(ui::update_text_layouts);
    world.run_system_once(ui::update_clipping);
    world.run_system_once(ui::compute_stack);
}

Status<snapshot::SnapshotError> rebuild_ui(World& world) {
    if (!world.has_resource<ui::Surface>() ||
        !world.has_resource<ui::LayoutState>() ||
        !world.has_resource<ui::Stack>() ||
        !world.has_resource<text::TextPipeline>()) {
        return failure(configuration_error(
            "UI rebuild resources are missing during snapshot restore"
        ));
    }

    world.resource<ui::Surface>() = ui::Surface {};
    world.resource<ui::LayoutState>() = ui::LayoutState {};
    world.resource<ui::Stack>() = ui::Stack {};
    world.resource<text::TextPipeline>() = text::TextPipeline {};
    reset_transient_state(world);
    synchronize_widget_shapes(world);
    rebuild_core_ui(world);
    rebuild_positioned_widgets(world);
    return {};
}

} // namespace

Status<snapshot::SnapshotError>
configure_ui_adapters(World& world, snapshot::SnapshotRegistry& registry) {
    if (!world.has_resource<ui::Surface>() ||
        !world.has_resource<ui::LayoutState>() ||
        !world.has_resource<ui::Stack>() ||
        !world.has_resource<text::TextPipeline>() ||
        !world.has_resource<Assets<Image>>() ||
        !world.has_resource<Assets<text::Font>>() ||
        !world.has_resource<input_focus::InputFocus>() ||
        !world.has_resource<input_focus::InputFocusVisible>() ||
        !world.has_resource<Window>()) {
        return failure(configuration_error(
            "UI snapshot adapter requires UiPlugin and its dependencies"
        ));
    }
    if (registry.codecs().find(type_id<Handle<Image>>()) == nullptr ||
        registry.codecs().find(type_id<Handle<text::Font>>()) == nullptr) {
        return failure(configuration_error(
            "Configure asset snapshot adapters before UI snapshot adapters"
        ));
    }

    registry.resource<Window>(snapshot::ResourcePolicy::Ignore);
    registry.resource<ui::Surface>(snapshot::ResourcePolicy::Rebuild);
    registry.resource<ui::LayoutState>(snapshot::ResourcePolicy::Rebuild);
    registry.resource<ui::Stack>(snapshot::ResourcePolicy::Rebuild);
    registry.resource<text::TextPipeline>(snapshot::ResourcePolicy::Rebuild);
    registry.resource<input_focus::InputFocus>(
        snapshot::ResourcePolicy::Snapshot
    );
    registry.resource<input_focus::InputFocusVisible>(
        snapshot::ResourcePolicy::Snapshot
    );

    registry.component<ui::ComputedNode>(snapshot::ComponentPolicy::Rebuild);
    registry.component<ui::CalculatedClip>(snapshot::ComponentPolicy::Rebuild);
    registry.component<ui::ComputedStackIndex>(
        snapshot::ComponentPolicy::Rebuild
    );
    registry.component<ui::ContentSize>(snapshot::ComponentPolicy::Rebuild);
    registry.component<ui::ImageNodeSize>(snapshot::ComponentPolicy::Rebuild);
    registry.component<text::TextLayoutInfo>(
        snapshot::ComponentPolicy::Rebuild
    );
    registry.component<ui::TextNodeFlags>(snapshot::ComponentPolicy::Rebuild);

    register_value_change_reflection<bool>("BoolValueChange");
    register_value_change_reflection<Entity>("EntityValueChange");
    register_value_change_reflection<float>("FloatValueChange");
    register_value_change_reflection<std::string>("StringValueChange");
    if (!configure_event_if_present<input_focus::FocusGained>(
            world,
            registry
        ) ||
        !configure_event_if_present<input_focus::FocusLost>(world, registry) ||
        !configure_event_if_present<text::TextChanged>(world, registry) ||
        !configure_event_if_present<ui_widgets::Activate>(world, registry) ||
        !configure_event_if_present<ui_widgets::ValueChange<bool>>(
            world,
            registry
        ) ||
        !configure_event_if_present<ui_widgets::SetChecked>(world, registry) ||
        !configure_event_if_present<ui_widgets::ToggleChecked>(
            world,
            registry
        ) ||
        !configure_event_if_present<ui_widgets::ValueChange<Entity>>(
            world,
            registry
        ) ||
        !configure_event_if_present<ui_widgets::SetListSelection>(
            world,
            registry
        ) ||
        !configure_event_if_present<ui_widgets::SelectionChange>(
            world,
            registry
        ) ||
        !configure_event_if_present<ui_widgets::MenuEvent>(world, registry) ||
        !configure_event_if_present<ui_widgets::ValueChange<float>>(
            world,
            registry
        ) ||
        !configure_event_if_present<ui_widgets::SetSliderValue>(
            world,
            registry
        ) ||
        !configure_event_if_present<ui_widgets::ScrollIntoView>(
            world,
            registry
        ) ||
        !configure_event_if_present<ui_widgets::ValueChange<std::string>>(
            world,
            registry
        ) ||
        !configure_event_if_present<ui_widgets::TextSubmit>(world, registry)) {
        return failure(
            configuration_error("Failed to register a UI event snapshot codec")
        );
    }

    registry.on_validate_capture([](const World& current) {
        return validate_rebuild_sources(current);
    });
    registry.on_after_restore([](World& restored) {
        return rebuild_ui(restored);
    });
    registry.on_restore_rollback([](World& restored) {
        return rebuild_ui(restored);
    });
    return {};
}

} // namespace ets::snapshot_runtime_ui
