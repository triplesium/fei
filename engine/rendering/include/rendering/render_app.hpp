#pragma once

#include "app/app.hpp"
#include "app/sub_app_runner.hpp"
#include "ecs/change_detection.hpp"
#include "ecs/query.hpp"
#include "rendering/extract.hpp"

#include <concepts>
#include <functional>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace fei {

// Label for the rendering SubApp owned by App.
struct RenderApp {};

// Render execution is selected by the graphics backend or explicitly when the
// Render App is installed. RenderingPlugin falls back to the inline runner.
using RenderRunner = SubAppRunner;
using InlineRenderRunner = InlineSubAppRunner;
using ThreadedRenderRunner = ThreadedSubAppRunner;
using RenderRunnerFactory =
    std::move_only_function<std::unique_ptr<RenderRunner>(SubApp)>;

inline constexpr ScheduleId RenderExtract =
    stable_type_hash("fei::RenderExtract");
inline constexpr ScheduleId RenderStartup =
    stable_type_hash("fei::RenderStartup");

struct MainEntity {
    Entity entity;

    [[nodiscard]] Entity id() const { return entity; }
    bool operator==(const MainEntity&) const = default;
};

struct RenderEntity {
    Entity entity;

    [[nodiscard]] Entity id() const { return entity; }
    bool operator==(const RenderEntity&) const = default;
};

struct SyncToRenderWorld {};

struct RenderEntityMap {
    std::unordered_map<Entity, Entity> main_to_render;
};

struct RenderExtractRegistry {
    std::unordered_set<TypeId> component_types;
    std::unordered_set<TypeId> reverse_component_types;
    std::unordered_set<TypeId> resource_types;
};

namespace detail {

template<typename T>
struct ExtractComponentState {
    std::unordered_set<Entity> previous_entities;
    std::size_t previous_count {0};
};

template<typename T>
    requires std::copy_constructible<T>
void extract_component(
    Extract<Query<Entity, const RenderEntity, const T>> source,
    ResRO<RenderEntityMap> entity_map,
    ResRW<ExtractComponentState<T>> state,
    Commands commands
) {
    std::unordered_set<Entity> current_entities;
    current_entities.reserve(state->previous_count);
    std::vector<std::pair<Entity, T>> extracted_components;
    extracted_components.reserve(state->previous_count);

    for (const auto& [main_entity, render_entity, component] : source.get()) {
        extracted_components.emplace_back(render_entity.id(), component);
        current_entities.insert(main_entity);
    }

    std::vector<Entity> removed_components;
    removed_components.reserve(
        state->previous_entities.size() -
        std::min(state->previous_entities.size(), current_entities.size())
    );
    for (const auto main_entity : state->previous_entities) {
        if (current_entities.contains(main_entity)) {
            continue;
        }
        const auto mapped = entity_map->main_to_render.find(main_entity);
        if (mapped != entity_map->main_to_render.end()) {
            removed_components.push_back(mapped->second);
        }
    }
    state->previous_count = current_entities.size();
    state->previous_entities = std::move(current_entities);

    if (!extracted_components.empty()) {
        commands.insert_batch<T>(std::move(extracted_components));
    }
    if (!removed_components.empty()) {
        commands.remove_batch<T>(std::move(removed_components));
    }
}

} // namespace detail

void install_render_app(App& app);
void install_render_app(App& app, RenderRunnerFactory runner_factory);

template<typename T>
    requires std::copy_constructible<T>
App& add_extract_component(App& app) {
    auto& render_app = app.sub_app<RenderApp>();
    auto& registry = render_app.resource<RenderExtractRegistry>();
    if (!registry.component_types.insert(type_id<T>()).second) {
        return app;
    }

    render_app.add_resource(detail::ExtractComponentState<T> {})
        .add_systems(RenderExtract, detail::extract_component<T>);
    return app;
}

template<typename T>
    requires std::move_constructible<T>
App& add_render_to_main_component(App& app) {
    auto& render_app = app.sub_app<RenderApp>();
    auto& registry = render_app.resource<RenderExtractRegistry>();
    if (!registry.reverse_component_types.insert(type_id<T>()).second) {
        return app;
    }

    render_app.add_post_update([](World& main_world, World& render_world) {
        struct Transfer {
            Entity render_entity;
            Optional<Entity> main_entity;
            T component;
        };

        std::vector<Transfer> transfers;
        auto query = Query<Entity, T>::get_param(
            render_world,
            SystemTicks {
                .last_run = 0,
                .this_run = render_world.read_change_tick(),
            }
        );
        for (auto [render_entity, component] : query) {
            Optional<Entity> main_entity;
            if (render_world.has_component<MainEntity>(render_entity)) {
                main_entity =
                    render_world.get_component<MainEntity>(render_entity)
                        .entity;
            }
            transfers.push_back(
                Transfer {
                    .render_entity = render_entity,
                    .main_entity = main_entity,
                    .component = std::move(component.write()),
                }
            );
        }

        for (auto& transfer : transfers) {
            auto target = transfer.main_entity &&
                                  main_world.has_entity(*transfer.main_entity) ?
                              *transfer.main_entity :
                              main_world.entity();
            main_world.add_component(target, std::move(transfer.component));
            if (render_world.has_entity(transfer.render_entity)) {
                render_world.despawn(transfer.render_entity);
            }
        }
    });
    return app;
}

} // namespace fei
