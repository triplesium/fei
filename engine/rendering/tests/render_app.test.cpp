#include "rendering/render_app.hpp"

#include "app/app.hpp"
#include "rendering/extract_resource.hpp"

#include <catch2/catch_test_macros.hpp>
#include <thread>

using namespace ets;

namespace ets {

struct MainRenderSettings {
    int value {0};
};

struct ExtractedRenderSettings {
    int value {0};
};

template<>
struct ExtractResource<ExtractedRenderSettings> {
    using Source = MainRenderSettings;

    static ExtractedRenderSettings extract_resource(const Source& source) {
        return ExtractedRenderSettings {.value = source.value * 2};
    }
};

} // namespace ets

namespace {

struct ExtractedA {
    int value {0};
};

struct ExtractedB {
    int value {0};
};

struct MainValue {
    int value {0};
};

struct ExtractObservation {
    int resource_value {0};
    int component_total {0};
};

struct ChangeObservation {
    int changed_count {0};
};

struct StartupResourceObservation {
    int value {0};
};

struct DeferredExtractThread {
    std::thread::id id;
};

struct DeferredExtractObservation {
    std::thread::id extract_thread;
    std::thread::id prepare_thread;
    std::thread::id apply_thread;
};

using ReadExtractQuery = Extract<Query<Entity, const ExtractedA>>;
using WriteExtractQuery = Query<Entity, ExtractedA>;

static_assert(ReadOnlySystemParam<Query<Entity, const ExtractedA>>);
static_assert(!ReadOnlySystemParam<WriteExtractQuery>);
static_assert(SystemParam<ReadExtractQuery>);

} // namespace

TEST_CASE(
    "Render app uses the backend-neutral inline runner",
    "[rendering][render-app][runner]"
) {
    App app;
    install_render_app(app);

    REQUIRE(
        app.sub_app_runner<RenderApp>().execution_mode() ==
        SubAppExecutionMode::Inline
    );
}

TEST_CASE(
    "Render app accepts a replaceable runner factory",
    "[rendering][render-app][runner]"
) {
    App app;
    bool factory_called = false;
    install_render_app(app, [&factory_called](SubApp render_app) {
        factory_called = true;
        return std::make_unique<InlineRenderRunner>(std::move(render_app));
    });

    REQUIRE(factory_called);
    REQUIRE(app.has_sub_app<RenderApp>());
    REQUIRE(
        app.sub_app_runner<RenderApp>().execution_mode() ==
        SubAppExecutionMode::Inline
    );
}

TEST_CASE(
    "Threaded render runner returns outputs at the next frame boundary",
    "[rendering][render-app][runner][threaded]"
) {
    struct ThreadedRenderOutput {
        int value {0};
    };

    App app;
    install_render_app(app, [](SubApp render_app) {
        return std::make_unique<ThreadedRenderRunner>(std::move(render_app));
    });
    add_render_to_main_component<ThreadedRenderOutput>(app);
    app.sub_app<RenderApp>().add_systems(RenderUpdate, [](Commands commands) {
        commands.spawn().add(ThreadedRenderOutput {.value = 23});
    });

    app.render();
    {
        auto outputs = Query<const ThreadedRenderOutput>::get_param(
            app.world(),
            SystemTicks {
                .last_run = 0,
                .this_run = app.world().read_change_tick(),
            }
        );
        REQUIRE(outputs.empty());
    }

    app.render();
    auto outputs = Query<const ThreadedRenderOutput>::get_param(
        app.world(),
        SystemTicks {
            .last_run = 0,
            .this_run = app.world().read_change_tick(),
        }
    );
    REQUIRE(outputs.size() == 1);
    REQUIRE(std::get<0>(outputs.first()).value == 23);
}

TEST_CASE(
    "Threaded render runner applies extract commands on its worker",
    "[rendering][render-app][extract][threaded]"
) {
    const auto caller_thread = std::this_thread::get_id();

    App app;
    install_render_app(app, [](SubApp render_app) {
        return std::make_unique<ThreadedRenderRunner>(std::move(render_app));
    });
    app.sub_app<RenderApp>()
        .add_resource(DeferredExtractObservation {})
        .add_systems(
            RenderExtract,
            [](Commands commands,
               ResRW<DeferredExtractObservation> observation) {
                observation->extract_thread = std::this_thread::get_id();
                commands.add_command([](World& world) {
                    world.add_resource(
                        DeferredExtractThread {.id = std::this_thread::get_id()}
                    );
                });
            }
        )
        .add_systems(
            RenderPrepare,
            [](ResRO<DeferredExtractThread> applied,
               ResRW<DeferredExtractObservation> observation) {
                observation->prepare_thread = std::this_thread::get_id();
                observation->apply_thread = applied->id;
            }
        );

    app.render();
    const auto& observation =
        app.sub_app<RenderApp>().resource<DeferredExtractObservation>();

    REQUIRE(observation.extract_thread == caller_thread);
    REQUIRE(observation.prepare_thread != caller_thread);
    REQUIRE(observation.apply_thread == observation.prepare_thread);
}

TEST_CASE(
    "ExtractResource snapshots changed main resources into the render world",
    "[rendering][render-app][extract-resource]"
) {
    App app;
    app.add_resource(MainRenderSettings {.value = 3});
    install_render_app(app);
    add_extract_resource<ExtractedRenderSettings>(app);
    add_extract_resource<ExtractedRenderSettings>(app);
    app.add_systems(StartUp, [](ResRW<MainRenderSettings> settings) {
        settings->value = 7;
    });
    app.sub_app<RenderApp>()
        .add_resource(StartupResourceObservation {})
        .add_systems(
            RenderStartup,
            [](ResRO<ExtractedRenderSettings> settings,
               ResRW<StartupResourceObservation> observation) {
                observation->value = settings->value;
            }
        );

    app.startup();

    auto& render_world = app.sub_app<RenderApp>().world();
    REQUIRE(render_world.has_local_resource<ExtractedRenderSettings>());
    REQUIRE_FALSE(app.world().has_resource<ExtractedRenderSettings>());
    REQUIRE(render_world.resource<StartupResourceObservation>().value == 14);

    render_world.resource<ExtractedRenderSettings>().value = 99;
    app.render();
    REQUIRE(render_world.resource<ExtractedRenderSettings>().value == 99);

    app.resource<MainRenderSettings>().value = 11;
    app.render();
    REQUIRE(render_world.resource<ExtractedRenderSettings>().value == 22);
}

TEST_CASE(
    "RenderExtract systems read parameters from the main world",
    "[rendering][render-app][extract]"
) {
    App app;
    app.add_resource(MainValue {.value = 12});
    const auto first = app.world().entity();
    const auto second = app.world().entity();
    app.world().add_component(first, ExtractedA {.value = 5});
    app.world().add_component(second, ExtractedA {.value = 8});

    install_render_app(app);
    app.sub_app<RenderApp>()
        .add_resource(ExtractObservation {})
        .add_systems(
            RenderExtract,
            [](Extract<ResRO<MainValue>> main_value,
               Extract<Query<Entity, const ExtractedA>> components,
               ResRW<ExtractObservation> observation) {
                observation->resource_value = (*main_value)->value;
                observation->component_total = 0;
                for (const auto& [_, component] : components.get()) {
                    observation->component_total += component.value;
                }
            }
        );

    app.render();

    const auto& observation =
        app.sub_app<RenderApp>().resource<ExtractObservation>();
    REQUIRE(observation.resource_value == 12);
    REQUIRE(observation.component_total == 13);
    REQUIRE(
        app.sub_app<RenderApp>().resource<ExtractMainWorld>().world == nullptr
    );
}

TEST_CASE(
    "Extract change filters use main world ticks",
    "[rendering][render-app][extract]"
) {
    App app;
    const auto entity = app.world().entity();
    app.world().add_component(entity, ExtractedA {.value = 1});

    install_render_app(app);
    app.sub_app<RenderApp>()
        .add_resource(ChangeObservation {})
        .add_systems(
            RenderExtract,
            [](Extract<Query<Entity, const ExtractedA>::Filter<
                   Changed<ExtractedA>>> changed,
               ResRW<ChangeObservation> observation) {
                observation->changed_count += static_cast<int>(changed->size());
            }
        );

    app.render();
    REQUIRE(
        app.sub_app<RenderApp>().resource<ChangeObservation>().changed_count ==
        1
    );

    app.render();
    REQUIRE(
        app.sub_app<RenderApp>().resource<ChangeObservation>().changed_count ==
        1
    );

    app.world().add_component(entity, ExtractedA {.value = 2});
    app.render();
    REQUIRE(
        app.sub_app<RenderApp>().resource<ChangeObservation>().changed_count ==
        2
    );
}

TEST_CASE(
    "Render app extracts components into an isolated world",
    "[rendering][render-app]"
) {
    App app;
    install_render_app(app);
    add_extract_component<ExtractedA>(app);

    const auto main_entity = app.world().entity();
    app.world().add_component(main_entity, ExtractedA {.value = 7});
    app.render();

    auto& render_world = app.sub_app<RenderApp>().world();
    const auto& entity_map = render_world.resource<RenderEntityMap>();
    REQUIRE(entity_map.main_to_render.contains(main_entity));
    const auto render_entity = entity_map.main_to_render.at(main_entity);
    REQUIRE(render_world.has_entity(render_entity));
    REQUIRE(
        render_world.get_component<MainEntity>(render_entity).entity ==
        main_entity
    );
    REQUIRE(app.world().has_component<SyncToRenderWorld>(main_entity));
    REQUIRE(
        app.world().get_component<RenderEntity>(main_entity).entity ==
        render_entity
    );
    REQUIRE(render_world.get_component<ExtractedA>(render_entity).value == 7);

    app.world().add_component(main_entity, ExtractedA {.value = 11});
    app.render();
    REQUIRE(render_world.get_component<ExtractedA>(render_entity).value == 11);
}

TEST_CASE(
    "Render app rebuilds synchronized entities when its source world changes",
    "[rendering][render-app][source]"
) {
    World play_world;
    App app;
    install_render_app(app);
    add_extract_component<ExtractedA>(app);
    app.sub_app<RenderApp>()
        .add_resource(ChangeObservation {})
        .add_systems(
            RenderExtract,
            [](Extract<Query<Entity, const ExtractedA>::Filter<
                   Changed<ExtractedA>>> changed,
               ResRW<ChangeObservation> observation) {
                observation->changed_count += static_cast<int>(changed->size());
            }
        );

    bool playing = false;
    app.set_sub_app_source<RenderApp>([&](World& editor_world) {
        return playing ? SubAppSource {&play_world, 1} :
                         SubAppSource {&editor_world, 0};
    });

    const auto editor_entity = app.world().entity();
    app.world().add_component(editor_entity, ExtractedA {.value = 7});
    const auto play_entity = play_world.entity();
    play_world.add_component(play_entity, ExtractedA {.value = 23});
    REQUIRE(editor_entity == play_entity);

    app.render();
    auto& render_world = app.sub_app<RenderApp>().world();
    auto& entity_map = render_world.resource<RenderEntityMap>();
    const auto editor_render_entity =
        entity_map.main_to_render.at(editor_entity);
    REQUIRE(
        render_world.get_component<ExtractedA>(editor_render_entity).value == 7
    );
    REQUIRE(render_world.resource<ChangeObservation>().changed_count == 1);

    playing = true;
    app.render();
    const auto play_render_entity = entity_map.main_to_render.at(play_entity);
    REQUIRE_FALSE(render_world.has_entity(editor_render_entity));
    REQUIRE(
        render_world.get_component<ExtractedA>(play_render_entity).value == 23
    );
    REQUIRE(
        play_world.get_component<RenderEntity>(play_entity).entity ==
        play_render_entity
    );
    REQUIRE(render_world.resource<ChangeObservation>().changed_count == 2);

    playing = false;
    app.render();
    const auto restored_render_entity =
        entity_map.main_to_render.at(editor_entity);
    REQUIRE_FALSE(render_world.has_entity(play_render_entity));
    REQUIRE(
        render_world.get_component<ExtractedA>(restored_render_entity).value ==
        7
    );
    REQUIRE(
        app.world().get_component<RenderEntity>(editor_entity).entity ==
        restored_render_entity
    );
    REQUIRE(render_world.resource<ChangeObservation>().changed_count == 3);
}

TEST_CASE(
    "Render app removes stale components while retaining synchronized entities",
    "[rendering][render-app]"
) {
    App app;
    install_render_app(app);
    add_extract_component<ExtractedA>(app);
    add_extract_component<ExtractedB>(app);

    const auto main_entity = app.world().entity();
    app.world().add_component(main_entity, ExtractedA {});
    app.world().add_component(main_entity, ExtractedB {});
    app.render();

    auto& render_world = app.sub_app<RenderApp>().world();
    auto& entity_map = render_world.resource<RenderEntityMap>();
    const auto render_entity = entity_map.main_to_render.at(main_entity);

    app.world().remove_component<ExtractedA>(main_entity);
    app.render();
    REQUIRE(render_world.has_entity(render_entity));
    REQUIRE_FALSE(render_world.has_component<ExtractedA>(render_entity));
    REQUIRE(render_world.has_component<ExtractedB>(render_entity));

    app.world().remove_component<SyncToRenderWorld>(main_entity);
    app.render();
    REQUIRE(app.world().has_component<SyncToRenderWorld>(main_entity));
    REQUIRE(render_world.has_entity(render_entity));

    app.world().remove_component<ExtractedB>(main_entity);
    app.render();
    REQUIRE(render_world.has_entity(render_entity));
    REQUIRE_FALSE(render_world.has_component<ExtractedB>(render_entity));
    REQUIRE(entity_map.main_to_render.contains(main_entity));

    app.world().remove_component<SyncToRenderWorld>(main_entity);
    app.render();
    REQUIRE_FALSE(render_world.has_entity(render_entity));
    REQUIRE_FALSE(entity_map.main_to_render.contains(main_entity));
    REQUIRE_FALSE(app.world().has_component<RenderEntity>(main_entity));
}

TEST_CASE(
    "Render app despawns synchronized entities with their main entities",
    "[rendering][render-app]"
) {
    App app;
    install_render_app(app);
    add_extract_component<ExtractedA>(app);

    const auto main_entity = app.world().entity();
    app.world().add_component(main_entity, ExtractedA {});
    app.render();

    auto& render_world = app.sub_app<RenderApp>().world();
    auto& entity_map = render_world.resource<RenderEntityMap>();
    const auto render_entity = entity_map.main_to_render.at(main_entity);

    app.world().despawn(main_entity);
    app.render();

    REQUIRE_FALSE(render_world.has_entity(render_entity));
    REQUIRE_FALSE(entity_map.main_to_render.contains(main_entity));
}

TEST_CASE(
    "Explicit sync markers create render entities before extraction",
    "[rendering][render-app]"
) {
    struct SyncObservation {
        Optional<Entity> render_entity;
    };

    App app;
    const auto main_entity = app.world().entity();
    app.world().add_component(main_entity, SyncToRenderWorld {});
    install_render_app(app);
    app.sub_app<RenderApp>()
        .add_resource(SyncObservation {})
        .add_systems(
            RenderExtract,
            [main_entity](
                Extract<Query<Entity, const RenderEntity>> synced,
                ResRW<SyncObservation> observation
            ) {
                for (const auto& [entity, render_entity] : synced.get()) {
                    if (entity == main_entity) {
                        observation->render_entity = render_entity.entity;
                    }
                }
            }
        );

    app.render();

    const auto& observation =
        app.sub_app<RenderApp>().resource<SyncObservation>();
    REQUIRE(observation.render_entity);
    REQUIRE(
        app.sub_app<RenderApp>().world().has_entity(*observation.render_entity)
    );
}

TEST_CASE(
    "Render app systems cannot implicitly borrow main world resources",
    "[rendering][render-app]"
) {
    struct SharedValue {
        int value {0};
    };
    struct RenderValue {
        bool direct_resource_visible {true};
        int extracted_value {0};
    };

    App app;
    app.add_resource(SharedValue {.value = 41});
    install_render_app(app);
    app.sub_app<RenderApp>()
        .add_resource(RenderValue {})
        .add_systems(
            RenderExtract,
            [](Extract<ResRO<SharedValue>> shared,
               ResRW<RenderValue> rendered) {
                rendered->extracted_value = (*shared)->value;
            }
        )
        .add_systems(
            RenderUpdate,
            [](Optional<ResRO<SharedValue>> shared,
               ResRW<RenderValue> rendered) {
                rendered->direct_resource_visible = shared.has_value();
            }
        );

    app.render();

    const auto& rendered = app.sub_app<RenderApp>().resource<RenderValue>();
    REQUIRE_FALSE(rendered.direct_resource_visible);
    REQUIRE(rendered.extracted_value == 41);
    REQUIRE_FALSE(app.world().has_resource<RenderValue>());
}

TEST_CASE(
    "Render app transfers selected output components back to the main world",
    "[rendering][render-app]"
) {
    struct RenderOutput {
        int value {0};
    };

    App app;
    install_render_app(app);
    add_render_to_main_component<RenderOutput>(app);
    app.sub_app<RenderApp>().add_systems(RenderUpdate, [](Commands commands) {
        commands.spawn().add(RenderOutput {.value = 17});
    });

    app.render();

    auto query = Query<const RenderOutput>::get_param(
        app.world(),
        SystemTicks {
            .last_run = 0,
            .this_run = app.world().read_change_tick(),
        }
    );
    REQUIRE(query.size() == 1);
    REQUIRE(std::get<0>(query.first()).value == 17);
}

TEST_CASE(
    "Threaded render output returns to the frame source before switching",
    "[rendering][render-app][source][threaded][output]"
) {
    struct SourceOutput {
        int value {0};
    };

    World play_world;
    App app;
    install_render_app(app, [](SubApp render_app) {
        return std::make_unique<ThreadedRenderRunner>(std::move(render_app));
    });
    add_render_to_main_component<SourceOutput>(app);
    app.sub_app<RenderApp>().add_systems(RenderUpdate, [](Commands commands) {
        commands.spawn().add(SourceOutput {.value = 31});
    });

    bool playing = false;
    app.set_sub_app_source<RenderApp>([&](World& editor_world) {
        return playing ? SubAppSource {&play_world, 1} :
                         SubAppSource {&editor_world, 0};
    });

    app.render();
    playing = true;
    app.render();

    auto editor_outputs = Query<const SourceOutput>::get_param(
        app.world(),
        SystemTicks {
            .last_run = 0,
            .this_run = app.world().read_change_tick(),
        }
    );
    REQUIRE(editor_outputs.size() == 1);
    REQUIRE(std::get<0>(editor_outputs.first()).value == 31);
    auto pending_play_outputs = Query<const SourceOutput>::get_param(
        play_world,
        SystemTicks {
            .last_run = 0,
            .this_run = play_world.read_change_tick(),
        }
    );
    REQUIRE(pending_play_outputs.empty());

    app.render();
    auto play_outputs = Query<const SourceOutput>::get_param(
        play_world,
        SystemTicks {
            .last_run = 0,
            .this_run = play_world.read_change_tick(),
        }
    );
    REQUIRE(play_outputs.size() == 1);
    REQUIRE(std::get<0>(play_outputs.first()).value == 31);

    app.synchronize_sub_app<RenderApp>();
}

TEST_CASE(
    "Render output preserves linked render entities",
    "[rendering][render-app][output]"
) {
    struct LinkedOutput {
        int value {0};
    };

    App app;
    install_render_app(app);
    add_extract_component<ExtractedA>(app);
    add_render_to_main_component<LinkedOutput>(app);
    app.sub_app<RenderApp>().add_systems(
        RenderUpdate,
        [](Query<Entity, const MainEntity> linked, Commands commands) {
            for (const auto& [entity, _] : linked) {
                commands.entity(entity).add(LinkedOutput {.value = 29});
            }
        }
    );

    const auto main_entity = app.world().entity();
    app.world().add_component(main_entity, ExtractedA {});
    app.render();

    auto& render_world = app.sub_app<RenderApp>().world();
    const auto render_entity =
        render_world.resource<RenderEntityMap>().main_to_render.at(main_entity);
    REQUIRE(render_world.has_entity(render_entity));
    REQUIRE_FALSE(render_world.has_component<LinkedOutput>(render_entity));
    REQUIRE(app.world().get_component<LinkedOutput>(main_entity).value == 29);
    REQUIRE(
        app.world().get_component<RenderEntity>(main_entity).entity ==
        render_entity
    );
}

TEST_CASE(
    "Render output groups transient components on one main entity",
    "[rendering][render-app][output]"
) {
    struct FirstOutput {
        int value {0};
    };
    struct SecondOutput {
        int value {0};
    };
    struct SpawnedOutput {
        Optional<Entity> entity;
    };

    App app;
    install_render_app(app);
    add_render_to_main_component<FirstOutput>(app);
    add_render_to_main_component<SecondOutput>(app);
    app.sub_app<RenderApp>()
        .add_resource(SpawnedOutput {})
        .add_systems(
            RenderUpdate,
            [](Commands commands, ResRW<SpawnedOutput> spawned) {
                auto entity = commands.spawn().add(
                    FirstOutput {.value = 3},
                    SecondOutput {.value = 5}
                );
                spawned->entity = entity.id();
            }
        );

    app.render();

    auto outputs =
        Query<Entity, const FirstOutput, const SecondOutput>::get_param(
            app.world(),
            SystemTicks {
                .last_run = 0,
                .this_run = app.world().read_change_tick(),
            }
        );
    REQUIRE(outputs.size() == 1);
    const auto& [_, first, second] = outputs.first();
    REQUIRE(first.value == 3);
    REQUIRE(second.value == 5);

    auto& render_world = app.sub_app<RenderApp>().world();
    const auto spawned = render_world.resource<SpawnedOutput>().entity;
    REQUIRE(spawned);
    REQUIRE_FALSE(render_world.has_entity(*spawned));
}
