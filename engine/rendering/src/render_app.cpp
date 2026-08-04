#include "rendering/render_app.hpp"

#include "app/sub_app.hpp"
#include "profiling/profiling.hpp"

#include <algorithm>
#include <vector>

namespace fei {

namespace {

class ExtractScope {
  public:
    ExtractScope(World& main_world, World& render_world) :
        m_main_world(main_world), m_render_world(render_world) {
        m_render_world.resource<ExtractMainWorld>().world = &m_main_world;
    }

    ExtractScope(const ExtractScope&) = delete;
    ExtractScope& operator=(const ExtractScope&) = delete;

    ~ExtractScope() {
        m_render_world.resource<ExtractMainWorld>().world = nullptr;
    }

  private:
    World& m_main_world;
    World& m_render_world;
};

void ensure_sync_markers(World& main_world, const World& render_world) {
    const auto& registry = render_world.resource<RenderExtractRegistry>();
    std::vector<Entity> entities_to_sync;

    for (const auto& [_, archetype] : main_world.archetypes()) {
        if (archetype.has_component(type_id<SyncToRenderWorld>())) {
            continue;
        }
        const auto requires_sync = std::ranges::any_of(
            registry.component_types,
            [&archetype](TypeId component) {
                return archetype.has_component(component);
            }
        );
        if (!requires_sync) {
            continue;
        }
        entities_to_sync.insert(
            entities_to_sync.end(),
            archetype.entities().begin(),
            archetype.entities().end()
        );
    }

    for (const auto entity : entities_to_sync) {
        if (main_world.has_entity(entity) &&
            !main_world.has_component<SyncToRenderWorld>(entity)) {
            main_world.add_component(entity, SyncToRenderWorld {});
        }
    }
}

void remove_stale_render_entities(World& main_world, World& render_world) {
    auto& entity_map = render_world.resource<RenderEntityMap>();
    for (auto entry = entity_map.main_to_render.begin();
         entry != entity_map.main_to_render.end();) {
        const auto main_entity = entry->first;
        const auto render_entity = entry->second;
        if (main_world.has_entity(main_entity) &&
            main_world.has_component<SyncToRenderWorld>(main_entity)) {
            ++entry;
            continue;
        }
        if (render_world.has_entity(render_entity)) {
            render_world.despawn(render_entity);
        }
        if (main_world.has_entity(main_entity) &&
            main_world.has_component<RenderEntity>(main_entity)) {
            main_world.remove_component<RenderEntity>(main_entity);
        }
        entry = entity_map.main_to_render.erase(entry);
    }
}

void sync_render_entities(World& main_world, World& render_world) {
    remove_stale_render_entities(main_world, render_world);

    std::vector<Entity> synced_entities;
    for (const auto& [_, archetype] : main_world.archetypes()) {
        if (!archetype.has_component(type_id<SyncToRenderWorld>())) {
            continue;
        }
        synced_entities.insert(
            synced_entities.end(),
            archetype.entities().begin(),
            archetype.entities().end()
        );
    }

    auto& entity_map = render_world.resource<RenderEntityMap>();
    for (const auto main_entity : synced_entities) {
        Optional<Entity> linked_entity;
        if (main_world.has_component<RenderEntity>(main_entity)) {
            linked_entity =
                main_world.get_component<RenderEntity>(main_entity).entity;
            if (!render_world.has_entity(*linked_entity)) {
                main_world.remove_component<RenderEntity>(main_entity);
                linked_entity = nullopt;
            }
        }

        const auto mapped = entity_map.main_to_render.find(main_entity);
        if (linked_entity && mapped != entity_map.main_to_render.end() &&
            mapped->second != *linked_entity &&
            render_world.has_entity(mapped->second)) {
            render_world.despawn(mapped->second);
        }

        if (!linked_entity && mapped != entity_map.main_to_render.end() &&
            render_world.has_entity(mapped->second)) {
            linked_entity = mapped->second;
        }

        if (!linked_entity) {
            linked_entity = render_world.entity();
        }

        const auto render_entity = *linked_entity;
        if (!render_world.has_component<MainEntity>(render_entity) ||
            render_world.get_component<MainEntity>(render_entity).id() !=
                main_entity) {
            render_world.add_component(
                render_entity,
                MainEntity {.entity = main_entity}
            );
        }
        if (!main_world.has_component<RenderEntity>(main_entity) ||
            main_world.get_component<RenderEntity>(main_entity).id() !=
                render_entity) {
            main_world.add_component(
                main_entity,
                RenderEntity {.entity = render_entity}
            );
        }
        entity_map.main_to_render.insert_or_assign(main_entity, render_entity);
    }
}

void run_render_extract(World& main_world, World& render_world) {
    ensure_sync_markers(main_world, render_world);
    sync_render_entities(main_world, render_world);

    ExtractScope scope(main_world, render_world);
    render_world.run_schedule(RenderExtract);
}

} // namespace

void install_render_app(App& app, RenderRunnerFactory runner_factory) {
    register_profile_schedule_name(RenderExtract, "RenderExtract");
    register_profile_schedule_name(RenderStartup, "RenderStartup");
    if (app.has_sub_app<RenderApp>()) {
        return;
    }
    if (!runner_factory) {
        fatal("Cannot install RenderApp with an empty runner factory");
    }

    SubApp render_app;
    render_app.world().set_schedule_apply_deferred(RenderExtract, false);
    render_app.add_startup_schedule(RenderStartup)
        .add_update_schedule(RenderPrepare)
        .add_update_schedule(RenderFirst)
        .add_update_schedule(RenderStart)
        .add_update_schedule(RenderUpdate)
        .add_update_schedule(RenderEnd)
        .add_update_schedule(RenderLast)
        .add_resource(ExtractMainWorld {})
        .add_resource(RenderEntityMap {})
        .add_resource(RenderExtractRegistry {})
        .add_resource(detail::RenderOutputTransferState {})
        .set_extract_before_startup()
        .set_pre_extract(run_render_extract)
        .add_post_update_cleanup(detail::cleanup_transient_render_outputs);

    app.insert_sub_app<RenderApp>(runner_factory(std::move(render_app)));
}

void install_render_app(App& app) {
    install_render_app(app, [](SubApp render_app) {
        return std::make_unique<InlineRenderRunner>(std::move(render_app));
    });
}

} // namespace fei
