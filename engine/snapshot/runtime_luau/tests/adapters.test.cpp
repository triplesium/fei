#include "snapshot_runtime_luau/adapters.hpp"

#include "app/app.hpp"
#include "ecs/dynamic/events.hpp"
#include "refl/cls.hpp"
#include "refl/registry.hpp"
#include "scripting_luau/plugin.hpp"
#include "scripting_luau/script_system_registry.hpp"
#include "scripting_luau/snapshot_state.hpp"

#include <catch2/catch_test_macros.hpp>
#include <string_view>

using namespace ets;

namespace {

constexpr std::string_view c_counter_type = "snapshot_counter.Counter";

struct ScriptTarget {
    int value {7};
};

void register_test_types() {
    static const bool registered = [] {
        Registry::instance()
            .register_cls<ScriptTarget>(
                {"snapshot_runtime_luau_test"},
                "ScriptTarget"
            )
            .add_property("value", &ScriptTarget::value);
        return true;
    }();
    (void)registered;
}

int counter_value(World& world) {
    auto type = Registry::instance().try_get_type(c_counter_type);
    REQUIRE(type);
    auto value = Registry::instance()
                     .get_cls(type->id())
                     .get_property("value")
                     .get(world.resource(type->id()));
    REQUIRE(value);
    return value->get<int>();
}

Entity counter_target(World& world) {
    auto type = Registry::instance().try_get_type(c_counter_type);
    REQUIRE(type);
    auto value = Registry::instance()
                     .get_cls(type->id())
                     .get_property("target")
                     .get(world.resource(type->id()));
    REQUIRE(value);
    return value->get<Entity>();
}

App make_script_app() {
    register_test_types();
    App app;
    app.add_plugin<LuauScriptingPlugin>();
    app.finish();
    app.resource<LuauScriptSystemRegistry>().queue_source(
        LuauScriptSource {
            .name = "snapshot_counter.luau",
            .content = R"(
                local function tick(counter: ResRW<Counter>)
                    counter.value += 1
                end

                export type Counter = {
                    value: i32,
                    target: entity,
                }

                export local SnapshotCounterPlugin = Plugin.new {
                    build = function(app: App)
                        app:insert_resource(Counter {})
                        app:add_system(Update, tick)
                    end,
                }
            )",
        }
    );
    app.run_schedule(PreUpdate);
    REQUIRE(app.resource<LuauScriptSystemRegistry>().queue_errors().empty());
    app.run_schedule(Update);

    const auto target = app.world().entity();
    app.world().add_component(target, ScriptTarget {});
    auto counter_type = Registry::instance().try_get_type(c_counter_type);
    REQUIRE(counter_type);
    const Entity target_ref = target;
    REQUIRE(
        Registry::instance()
            .get_cls(counter_type->id())
            .get_property("target")
            .set(app.world().resource(counter_type->id()), Ref(target_ref))
    );
    return app;
}

void ignore_unconfigured_resources(
    World& world,
    snapshot::SnapshotRegistry& registry
) {
    for (const auto type : world.resource_types()) {
        if (!registry.resource_policy(type)) {
            registry.set_resource_policy(
                type,
                snapshot::ResourcePolicy::Ignore
            );
        }
    }
}

void require_snapshot_ready(
    World& world,
    const snapshot::SnapshotRegistry& registry
) {
    const auto coverage = snapshot::audit(world, registry);
    std::string audit_errors;
    for (const auto& resource : coverage.resources) {
        if (!resource.serializable) {
            audit_errors += resource.type_name + ": " + resource.message + "\n";
        }
    }
    INFO(audit_errors);
    REQUIRE(coverage.ready);
    REQUIRE(coverage.complete);
}

} // namespace

TEST_CASE(
    "Luau snapshot adapter restores script-declared ECS resources",
    "[snapshot][runtime][luau][determinism]"
) {
    auto app = make_script_app();
    REQUIRE(counter_value(app.world()) == 1);

    snapshot::CheckpointStore checkpoints;
    REQUIRE(
        snapshot_runtime_luau::configure_luau_adapters(
            app.world(),
            checkpoints.registry()
        )
    );
    auto counter_type = Registry::instance().try_get_type(c_counter_type);
    REQUIRE(counter_type);
    const auto counter_policy =
        checkpoints.registry().resource_policy(counter_type->id());
    REQUIRE(counter_policy);
    CHECK(*counter_policy == snapshot::ResourcePolicy::Snapshot);
    const auto dynamic_events_policy =
        checkpoints.registry().resource_policy(type_id<DynamicEvents>());
    REQUIRE(dynamic_events_policy);
    CHECK(*dynamic_events_policy == snapshot::ResourcePolicy::Snapshot);
    ignore_unconfigured_resources(app.world(), checkpoints.registry());
    require_snapshot_ready(app.world(), checkpoints.registry());
    auto created = checkpoints.create("script-start", app.world(), true);
    const auto create_message =
        created ? std::string {} :
                  created.error().path + ": " + created.error().message;
    INFO(create_message);
    REQUIRE(created);

    app.run_schedule(Update);
    app.run_schedule(Update);
    REQUIRE(counter_value(app.world()) == 3);
    const auto original_target = counter_target(app.world());

    REQUIRE(checkpoints.restore("script-start", app.world()));
    REQUIRE(counter_value(app.world()) == 1);
    const auto restored_target = counter_target(app.world());
    CHECK(restored_target != original_target);
    REQUIRE(app.world().has_entity(restored_target));
    CHECK(app.world().get_component<ScriptTarget>(restored_target).value == 7);
    app.run_schedule(Update);
    app.run_schedule(Update);
    CHECK(counter_value(app.world()) == 3);
}

TEST_CASE(
    "Luau snapshot adapter rejects checkpoints after module changes",
    "[snapshot][runtime][luau][generation]"
) {
    auto app = make_script_app();
    snapshot::CheckpointStore checkpoints;
    REQUIRE(
        snapshot_runtime_luau::configure_luau_adapters(
            app.world(),
            checkpoints.registry()
        )
    );
    ignore_unconfigured_resources(app.world(), checkpoints.registry());
    require_snapshot_ready(app.world(), checkpoints.registry());
    auto created = checkpoints.create("before-reload", app.world(), true);
    const auto create_message =
        created ? std::string {} :
                  created.error().path + ": " + created.error().message;
    INFO(create_message);
    REQUIRE(created);

    app.run_schedule(Update);
    const auto current_counter = counter_value(app.world());
    const auto generation_before = app.resource<LuauSnapshotState>().generation;
    app.resource<LuauScriptSystemRegistry>().queue_source(
        LuauScriptSource {
            .name = "additional_module.luau",
            .content = R"(
                export type AdditionalState = {
                    value: i32,
                }
            )",
        }
    );
    app.run_schedule(PreUpdate);
    REQUIRE(app.resource<LuauSnapshotState>().generation > generation_before);

    auto restored = checkpoints.restore("before-reload", app.world());
    REQUIRE_FALSE(restored);
    CHECK(
        restored.error().kind ==
        snapshot::SnapshotError::Kind::RestoreHookFailed
    );
    CHECK(restored.error().message.find("generation") != std::string::npos);
    CHECK(counter_value(app.world()) == current_counter);
    CHECK(
        app.resource<LuauSnapshotState>().generation ==
        app.resource<LuauScriptSystemRegistry>().snapshot_generation()
    );
}
