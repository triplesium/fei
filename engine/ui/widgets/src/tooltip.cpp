#include "ui_widgets/plugin.hpp"

namespace ets::ui_widgets {

void sync_tooltips(
    Query<Entity, const Tooltip>::Filter<Without<ui::Interaction>>
        missing_interactions,
    Query<Entity, const Tooltip>::Filter<Without<TooltipState>> missing_states,
    Query<Entity, const Tooltip> tooltips,
    Query<Entity, const Popover> popovers,
    Query<Entity, ui::Node> nodes,
    Commands commands
) {
    for (const auto& [entity, tooltip] : missing_interactions) {
        (void)tooltip;
        commands.entity(entity).add(ui::Interaction::None);
    }
    for (const auto& [entity, tooltip] : missing_states) {
        commands.entity(entity).add(TooltipState {});
        if (const auto content = nodes.get(tooltip.content)) {
            auto node = std::get<1>(*content);
            auto next = node.read();
            next.display = ui::Display::None;
            node = next;
        } else {
            commands.add_command([content = tooltip.content](World& world) {
                if (!world.has_component<ui::Node>(content)) {
                    world.add_component(
                        content,
                        ui::Node {
                            .display = ui::Display::None,
                            .position_type = ui::PositionType::Absolute,
                        }
                    );
                }
            });
        }
    }
    for (const auto& [entity, tooltip] : tooltips) {
        if (!popovers.get(tooltip.content)) {
            commands.entity(tooltip.content)
                .add(
                    Popover {
                        .anchor = entity,
                        .placement = tooltip.placement,
                    }
                );
        }
    }
}

void update_tooltips(
    Query<Entity, const Tooltip, const ui::Interaction, TooltipState> tooltips,
    Query<Entity, ui::Node> nodes,
    ResRO<Time> time
) {
    for (auto [entity, tooltip, interaction, state] : tooltips) {
        (void)entity;
        const bool hovered = interaction != ui::Interaction::None;
        auto next_state = state.read();
        if (hovered) {
            next_state.hovered_time += time->delta();
            next_state.visible = next_state.hovered_time >= tooltip.delay;
        } else {
            next_state.hovered_time = 0.0f;
            next_state.visible = false;
        }
        if (next_state != state.read()) {
            state = next_state;
        }
        if (const auto content = nodes.get(tooltip.content)) {
            auto node = std::get<1>(*content);
            const auto display =
                next_state.visible ? ui::Display::Flex : ui::Display::None;
            if (node.read().display != display) {
                auto next_node = node.read();
                next_node.display = display;
                node = next_node;
            }
        }
    }
}

} // namespace ets::ui_widgets
