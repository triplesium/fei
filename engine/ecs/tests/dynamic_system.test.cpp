#include "ecs/annotations.hpp"
#include "ecs/dynamic/commands.hpp"
#include "ecs/dynamic/events.hpp"
#include "ecs/dynamic/query.hpp"
#include "ecs/dynamic/removed_components.hpp"
#include "ecs/dynamic/resource.hpp"
#include "ecs/dynamic/system.hpp"
#include "ecs/dynamic/system_decl.hpp"
#include "ecs/world.hpp"
#include "refl/generated.hpp"
#include "refl/registry.hpp"
#include "test_types.hpp"

#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace ets;
using namespace ets::ecs_test;

namespace {

template<typename T>
std::unique_ptr<T> make_named_decl(std::string name) {
    auto decl = std::make_unique<T>();
    decl->name = std::move(name);
    return decl;
}

class MutatingDynamicExecutor final : public DynamicSystemExecutor {
  public:
    int calls {0};
    int rows {0};

    Status<DynamicSystemError> execute(const std::vector<Ref>& args) override {
        ++calls;
        REQUIRE(args.size() == 2);

        auto& config = args[0].get<GameConfig>();
        config.max_entities += 1;

        auto& query = args[1].get<DynamicQuery>();
        DynamicQueryCursor cursor;
        DynamicQueryRow row;
        while (query.next(cursor, row)) {
            auto position = query.field(row, 0);
            position.get<Position>().x += 2.0f;
            ++rows;
        }

        return {};
    }
};

struct CustomResourceAliasParamDecl final
    : DynamicSystemParamDeclBase<CustomResourceAliasParamDecl> {
    DynamicTypeRef type;
};

struct DynamicMainThreadResource {};

} // namespace

TEST_CASE(
    "ECS dynamic system decls compile params and access",
    "[ecs][dynamic]"
) {
    Registry::instance().register_type<GameConfig>();
    register_components();

    DynamicSystemDecl decl {
        .name = "decl_system",
        .schedule = TestSchedule,
    };
    auto resource = make_named_decl<DynamicResourceParamDecl>("config");
    resource->type = DynamicTypeRef {.type_name = "GameConfig"};
    resource->access = DynamicParamAccess::Write;
    decl.params.push_back(std::move(resource));

    auto query = make_named_decl<DynamicQueryParamDecl>("targets");
    query->fields.push_back(
        DynamicQueryFieldDecl {
            .name = "entity",
            .type = {},
            .kind = DynamicQueryFieldDeclKind::Entity,
        }
    );
    query->fields.push_back(
        DynamicQueryFieldDecl {
            .name = "position",
            .type = DynamicTypeRef {.type_id = type_id<Position>()},
            .kind = DynamicQueryFieldDeclKind::Component,
            .access = DynamicParamAccess::Write,
        }
    );
    query->filters.push_back(
        DynamicQueryFilterDecl {
            .type = DynamicTypeRef {.type_name = "Health"},
            .required = false,
        }
    );
    decl.params.push_back(std::move(query));

    decl.params.push_back(
        make_named_decl<DynamicCommandsParamDecl>("commands")
    );

    auto params = compile_dynamic_system_params(decl);
    REQUIRE(params);
    REQUIRE(params->size() == 3);

    auto access = dynamic_system_access_for_params(*params);
    REQUIRE(access.write_resources.contains(type_id<GameConfig>()));
    REQUIRE(access.write_components.contains(type_id<Position>()));
    REQUIRE_FALSE(access.write_components.contains(type_id<Health>()));
    REQUIRE_FALSE(access.read_components.contains(type_id<Entity>()));
    REQUIRE(access.commands);
    REQUIRE(access.write_resources.contains(type_id<CommandsQueue>()));
}

TEST_CASE(
    "ECS dynamic system param compilers can be externally registered",
    "[ecs][dynamic]"
) {
    Registry::instance().register_type<GameConfig>();

    DynamicSystemParamCompilerRegistry::instance()
        .add<CustomResourceAliasParamDecl>(
            [](const CustomResourceAliasParamDecl& decl) {
                auto type = resolve_dynamic_type_ref(decl.type);
                if (!type) {
                    return Result<DynamicSystemParamPtr, DynamicSystemError> {
                        failure(std::move(type.error()))
                    };
                }

                DynamicSystemParamPtr result =
                    std::make_unique<DynamicResourceParam>(
                        decl.name,
                        *type,
                        DynamicParamAccess::Read
                    );
                return Result<DynamicSystemParamPtr, DynamicSystemError> {
                    std::move(result)
                };
            }
        );

    DynamicSystemDecl decl {.name = "custom_decl_system"};
    auto custom = make_named_decl<CustomResourceAliasParamDecl>("config");
    custom->type = DynamicTypeRef {.type_id = type_id<GameConfig>()};
    decl.params.push_back(std::move(custom));

    auto params = compile_dynamic_system_params(decl);
    REQUIRE(params);

    auto access = dynamic_system_access_for_params(*params);
    REQUIRE(access.read_resources.contains(type_id<GameConfig>()));
    REQUIRE_FALSE(access.write_resources.contains(type_id<GameConfig>()));
}

TEST_CASE("ECS dynamic system decls reject invalid params", "[ecs][dynamic]") {
    DynamicSystemDecl missing_query_field {.name = "invalid_query"};
    missing_query_field.params.push_back(
        make_named_decl<DynamicQueryParamDecl>("targets")
    );

    auto query_result = compile_dynamic_system_params(missing_query_field);
    REQUIRE_FALSE(query_result);
    REQUIRE(
        query_result.error().message.find("must declare fields") !=
        std::string::npos
    );

    DynamicSystemDecl missing_resource_type {.name = "invalid_resource"};
    missing_resource_type.params.push_back(
        make_named_decl<DynamicResourceParamDecl>("config")
    );

    auto resource_result = compile_dynamic_system_params(missing_resource_type);
    REQUIRE_FALSE(resource_result);
    REQUIRE(
        resource_result.error().message.find("missing type") !=
        std::string::npos
    );

    struct UnregisteredParamDecl final
        : DynamicSystemParamDeclBase<UnregisteredParamDecl> {};

    UnregisteredParamDecl unregistered;
    auto compiler_result = compile_dynamic_system_param(unregistered);
    REQUIRE_FALSE(compiler_result);
    REQUIRE(
        compiler_result.error().message.find("not registered") !=
        std::string::npos
    );
}

TEST_CASE(
    "ECS dynamic resource params expose access and prepare refs",
    "[ecs][dynamic]"
) {
    Registry::instance().register_type<GameConfig>();
    Registry::instance().register_type<EventQueue>();

    World world;
    world.add_resource(GameConfig {.max_entities = 64, .dt = 0.25f});

    DynamicResourceParam read_param(
        "config",
        type_id<GameConfig>(),
        DynamicParamAccess::Read
    );
    auto read_access = read_param.access();
    REQUIRE(read_access.read_resources.contains(type_id<GameConfig>()));
    REQUIRE_FALSE(read_access.write_resources.contains(type_id<GameConfig>()));

    auto read_ref = read_param.prepare(world);
    REQUIRE(read_ref);
    REQUIRE(read_ref->is_const());
    REQUIRE(read_ref->get_const<GameConfig>().max_entities == 64);

    DynamicResourceParam write_param(
        "config",
        type_id<GameConfig>(),
        DynamicParamAccess::Write
    );
    auto write_access = write_param.access();
    REQUIRE(write_access.write_resources.contains(type_id<GameConfig>()));
    REQUIRE_FALSE(write_access.read_resources.contains(type_id<GameConfig>()));

    auto write_ref = write_param.prepare(world);
    REQUIRE(write_ref);
    REQUIRE_FALSE(write_ref->is_const());
    write_ref->get<GameConfig>().max_entities = 128;
    REQUIRE(world.resource<GameConfig>().max_entities == 128);

    DynamicResourceParam optional_missing(
        "events",
        type_id<EventQueue>(),
        DynamicParamAccess::Read,
        true
    );
    auto optional_ref = optional_missing.prepare(world);
    REQUIRE(optional_ref);
    REQUIRE_FALSE(static_cast<bool>(*optional_ref));

    DynamicResourceParam required_missing(
        "events",
        type_id<EventQueue>(),
        DynamicParamAccess::Read
    );
    auto missing_ref = required_missing.prepare(world);
    REQUIRE_FALSE(missing_ref);
    REQUIRE(
        missing_ref.error().message.find("missing resource") !=
        std::string::npos
    );
}

TEST_CASE(
    "ECS dynamic resource params preserve reflected thread affinity",
    "[ecs][dynamic][access]"
) {
    register_generated_reflection();
    Registry::instance().add_annotation<DynamicMainThreadResource>(
        annotations::Resource {.main_thread_only = true}
    );

    DynamicResourceParam read_param(
        "main_thread_resource",
        type_id<DynamicMainThreadResource>(),
        DynamicParamAccess::Read
    );
    const auto read_access = read_param.access();
    REQUIRE(read_access.read_resources.contains(
        type_id<DynamicMainThreadResource>()
    ));
    REQUIRE(read_access.main_thread_only);
    REQUIRE(read_access.is_barrier());

    DynamicResourceParam write_param(
        "main_thread_resource",
        type_id<DynamicMainThreadResource>(),
        DynamicParamAccess::Write
    );
    const auto write_access = write_param.access();
    REQUIRE(write_access.write_resources.contains(
        type_id<DynamicMainThreadResource>()
    ));
    REQUIRE(write_access.main_thread_only);
    REQUIRE(write_access.is_barrier());
}

TEST_CASE(
    "ECS dynamic query params expose component and entity fields",
    "[ecs][dynamic]"
) {
    register_components();

    World world;

    auto matched = world.entity();
    world.add_component(matched, Position(1.0f, 2.0f));
    world.add_component(matched, Velocity(3.0f, 4.0f));

    auto missing_velocity = world.entity();
    world.add_component(missing_velocity, Position(10.0f, 20.0f));

    auto filtered_out = world.entity();
    world.add_component(filtered_out, Position(100.0f, 200.0f));
    world.add_component(filtered_out, Velocity(30.0f, 40.0f));
    world.add_component(filtered_out, Health(50));

    DynamicQuery query(
        "movers",
        {
            DynamicQueryField {
                .name = "entity",
                .type = type_id<Entity>(),
                .kind = DynamicQueryFieldKind::Entity,
            },
            DynamicQueryField {
                .name = "position",
                .type = type_id<Position>(),
                .access = DynamicParamAccess::Write,
            },
            DynamicQueryField {
                .name = "velocity",
                .type = type_id<Velocity>(),
                .access = DynamicParamAccess::Read,
            },
        },
        {
            DynamicQueryFilter {
                .type = type_id<Health>(),
                .required = false,
            },
        }
    );

    auto access = query.access();
    REQUIRE(access.write_components.contains(type_id<Position>()));
    REQUIRE(access.read_components.contains(type_id<Velocity>()));
    REQUIRE_FALSE(access.read_components.contains(type_id<Entity>()));
    REQUIRE_FALSE(access.write_components.contains(type_id<Entity>()));
    REQUIRE_FALSE(access.read_components.contains(type_id<Health>()));
    REQUIRE_FALSE(access.write_components.contains(type_id<Health>()));

    auto prepared = query.prepare(world);
    REQUIRE(prepared);
    REQUIRE(prepared->try_get<DynamicQuery>() == &query);
    REQUIRE(query.size() == 1);

    DynamicQueryCursor cursor;
    DynamicQueryRow row;
    REQUIRE(query.next(cursor, row));

    auto entity = query.field(row, 0);
    REQUIRE(entity.is_const());
    REQUIRE(entity.get_const<Entity>() == matched);

    auto position = query.field(row, 1);
    REQUIRE_FALSE(position.is_const());
    position.get<Position>().x += 5.0f;

    auto velocity = query.field(row, 2);
    REQUIRE(velocity.is_const());
    REQUIRE(velocity.get_const<Velocity>() == Velocity(3.0f, 4.0f));

    REQUIRE_FALSE(query.next(cursor, row));
    REQUIRE(world.get_component<Position>(matched) == Position(6.0f, 2.0f));
    REQUIRE(
        world.get_component<Position>(missing_velocity) ==
        Position(10.0f, 20.0f)
    );
    REQUIRE(
        world.get_component<Position>(filtered_out) == Position(100.0f, 200.0f)
    );
}

TEST_CASE(
    "ECS dynamic commands params expose access and prepare commands",
    "[ecs][dynamic]"
) {
    Registry::instance().register_type<Position>();
    Registry::instance().register_type<CommandsQueue>();

    DynamicCommandsParam commands_param("commands");
    auto access = commands_param.access();
    REQUIRE(access.commands);
    REQUIRE(access.write_resources.contains(type_id<CommandsQueue>()));
    REQUIRE(access.is_barrier());

    World missing_queue_world;
    auto missing_commands = commands_param.prepare(missing_queue_world);
    REQUIRE_FALSE(missing_commands);
    REQUIRE(
        missing_commands.error().message.find("missing resource") !=
        std::string::npos
    );

    World world;
    world.add_resource(CommandsQueue {});

    auto commands_ref = commands_param.prepare(world);
    REQUIRE(commands_ref);
    auto* commands = commands_ref->try_get<Commands>();
    REQUIRE(commands != nullptr);

    auto spawned = commands->spawn().add(Position(5.0f, 6.0f)).id();
    world.resource<CommandsQueue>().execute(world);

    REQUIRE(world.has_entity(spawned));
    REQUIRE(world.has_component<Position>(spawned));
    REQUIRE(world.get_component<Position>(spawned) == Position(5.0f, 6.0f));
}

TEST_CASE(
    "ECS dynamic systems prepare params and execute callbacks",
    "[ecs][dynamic]"
) {
    Registry::instance().register_type<GameConfig>();
    Registry::instance().register_type<Position>();

    World world;
    world.add_resource(GameConfig {.max_entities = 9, .dt = 0.016f});
    auto entity = world.entity();
    world.add_component(entity, Position(3.0f, 4.0f));

    DynamicSystemParams params;
    params.push_back(
        std::make_unique<DynamicResourceParam>(
            "config",
            type_id<GameConfig>(),
            DynamicParamAccess::Write
        )
    );
    params.push_back(
        std::make_unique<DynamicQuery>(
            "positions",
            std::vector<DynamicQueryField> {
                DynamicQueryField {
                    .name = "position",
                    .type = type_id<Position>(),
                    .access = DynamicParamAccess::Write,
                },
            },
            std::vector<DynamicQueryFilter> {}
        )
    );

    auto executor = std::make_unique<MutatingDynamicExecutor>();
    auto* executor_ptr = executor.get();
    DynamicSystem system(
        "dynamic_test",
        std::move(params),
        std::move(executor)
    );

    const auto& access = system.access();
    REQUIRE(access.write_resources.contains(type_id<GameConfig>()));
    REQUIRE(access.write_components.contains(type_id<Position>()));

    system.run(world);

    REQUIRE(executor_ptr->calls == 1);
    REQUIRE(executor_ptr->rows == 1);
    REQUIRE(world.resource<GameConfig>().max_entities == 10);
    REQUIRE(world.get_component<Position>(entity) == Position(5.0f, 4.0f));
}

TEST_CASE(
    "ECS dynamic query change filters apply per row",
    "[ecs][dynamic][query][change_detection]"
) {
    register_components();
    World world;
    world.increment_change_tick();
    const auto added_entity = world.entity();
    world.add_component(added_entity, Position(1.0F, 2.0F));
    const auto old_entity = world.entity();
    world.add_component(old_entity, Position(3.0F, 4.0F));
    auto& old_ticks = world.archetypes()
                          .get(world.entity_location(old_entity)->archetype_id)
                          .component_ticks(
                              type_id<Position>(),
                              world.entity_location(old_entity)->row
                          );
    old_ticks.added = 0;
    old_ticks.changed = 0;

    DynamicQuery added(
        "added",
        {DynamicQueryField {
            .name = "entity",
            .type = type_id<Entity>(),
            .kind = DynamicQueryFieldKind::Entity,
        }},
        {DynamicQueryFilter {
            .kind = DynamicQueryFilter::Kind::Added,
            .type = type_id<Position>(),
        }}
    );
    REQUIRE(added.access().read_components.contains(type_id<Position>()));
    REQUIRE(added.prepare(
        world,
        SystemTicks {.last_run = 0, .this_run = world.read_change_tick()}
    ));
    REQUIRE(added.size() == 1);
    DynamicQueryCursor cursor;
    DynamicQueryRow row;
    REQUIRE(added.next(cursor, row));
    REQUIRE(added.field(row, 0).get_const<Entity>() == added_entity);

    DynamicQuery changed(
        "changed",
        {DynamicQueryField {
            .name = "entity",
            .type = type_id<Entity>(),
            .kind = DynamicQueryFieldKind::Entity,
        }},
        {DynamicQueryFilter {
            .kind = DynamicQueryFilter::Kind::Changed,
            .type = type_id<Position>(),
        }}
    );
    const auto last_run = world.read_change_tick();
    world.get_component_rw<Position>(old_entity)->x = 8.0F;
    REQUIRE(changed.prepare(
        world,
        SystemTicks {
            .last_run = last_run,
            .this_run = world.read_change_tick(),
        }
    ));
    REQUIRE(changed.size() == 1);
}

TEST_CASE(
    "ECS dynamic cursor params checkpoint and validate their types",
    "[ecs][dynamic][runtime_state]"
) {
    register_components();
    World world;
    const auto first = world.entity();
    const auto second = world.entity();
    world.add_component(first, Position {});
    world.add_component(second, Position {});
    world.remove_component<Position>(first);
    world.remove_component<Position>(second);

    DynamicRemovedComponents removed("removed", type_id<Position>());
    REQUIRE(removed.prepare(
        world,
        SystemTicks {.last_run = 0, .this_run = world.read_change_tick()}
    ));
    const auto first_removed = removed.next();
    REQUIRE(first_removed);
    REQUIRE(*first_removed == first);
    auto removed_state = removed.capture_runtime_state();
    REQUIRE(removed_state);
    REQUIRE(removed_state->kind == SystemParamRuntimeStateKind::Counter);
    removed.clear();
    REQUIRE_FALSE(removed.next());
    REQUIRE(removed.restore_runtime_state(*removed_state));
    const auto second_removed = removed.next();
    REQUIRE(second_removed);
    REQUIRE(*second_removed == second);

    DynamicEventParam reader(
        "events",
        type_id<Velocity>(),
        DynamicEventParamKind::ReaderRO
    );
    REQUIRE_FALSE(reader.validate_runtime_state(*removed_state));

    DynamicEventParam writer(
        "events",
        type_id<Velocity>(),
        DynamicEventParamKind::Writer
    );
    auto writer_state = writer.capture_runtime_state();
    REQUIRE(writer_state);
    REQUIRE(writer_state->kind == SystemParamRuntimeStateKind::Stateless);
    REQUIRE_FALSE(reader.validate_runtime_state(*writer_state));
}

TEST_CASE(
    "ECS dynamic event params require explicit type registration",
    "[ecs][dynamic][event]"
) {
    register_components();
    World world;
    DynamicEventParam optional_reader(
        "events",
        type_id<Velocity>(),
        DynamicEventParamKind::ReaderRO,
        true
    );
    auto optional_missing = optional_reader.prepare(world, {});
    REQUIRE(optional_missing);
    CHECK_FALSE(*optional_missing);

    DynamicEventParam writer(
        "events",
        type_id<Velocity>(),
        DynamicEventParamKind::Writer
    );
    REQUIRE_FALSE(writer.prepare(world, {}));

    auto& events = world.add_resource(DynamicEvents {});
    REQUIRE_FALSE(writer.prepare(world, {}));
    events.register_type(type_id<Velocity>());
    REQUIRE(events.registered(type_id<Velocity>()));
    REQUIRE(writer.prepare(world, {}));
    auto optional_registered = optional_reader.prepare(world, {});
    REQUIRE(optional_registered);
    CHECK(*optional_registered);
}
