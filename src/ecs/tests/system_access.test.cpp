#include "ecs/schedule.hpp"
#include "test_types.hpp"

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>
#include <type_traits>

using namespace fei;
using namespace fei::ecs_test;

namespace {

void resource_access_system(Res<GameConfig>, CRes<EventQueue>) {}

void write_position_system(Query<Position>) {}

void read_position_system(Query<const Position>) {}

void read_position_again_system(Query<const Position>) {}

void named_profile_system(Query<const Position>) {}

template<typename T>
void template_profile_system(Query<const T>) {}

template<typename T>
struct TemplateProfileSystems {
    static void static_profile_system(Query<const T>) {}
};

void read_velocity_system(Query<const Velocity>) {}

void read_position_write_velocity_system(
    Query<Entity, const Position, Velocity>
) {}

void commands_system(Commands) {}

void world_ref_system(WorldRef) {}

void event_access_system(EventReader<GameEvent>, EventWriter<PlayerMoved>) {}

struct MainThreadResource {};

void main_thread_resource_system(Res<MainThreadResource>) {}

struct CustomParam {
    static CustomParam get_param(World&) { return {}; }
};

void custom_param_system(CustomParam) {}

} // namespace

template<>
struct fei::SystemParamTraits<CustomParam>
    : fei::StatelessParamTraits<CustomParam> {};

template<>
struct fei::ResourceTraits<MainThreadResource> {
    static constexpr bool main_thread_only = true;
};

TEST_CASE("ECS systems expose resource access metadata", "[ecs][system]") {
    FunctionSystem<decltype(resource_access_system)*> system(
        resource_access_system
    );
    const auto& access = system.access();

    REQUIRE(access.write_resources.contains(type_id<GameConfig>()));
    REQUIRE(access.read_resources.contains(type_id<EventQueue>()));
    REQUIRE_FALSE(access.world_exclusive);
    REQUIRE_FALSE(access.main_thread_only);
    REQUIRE_FALSE(access.commands);
}

TEST_CASE("ECS systems expose query access metadata", "[ecs][system]") {
    FunctionSystem<decltype(read_position_write_velocity_system)*> system(
        read_position_write_velocity_system
    );
    const auto& access = system.access();

    REQUIRE(access.read_components.contains(type_id<Position>()));
    REQUIRE(access.write_components.contains(type_id<Velocity>()));
    REQUIRE_FALSE(access.read_components.contains(type_id<Entity>()));
    REQUIRE_FALSE(access.write_components.contains(type_id<Entity>()));
}

TEST_CASE("ECS named systems preserve system metadata", "[ecs][system]") {
    SystemConfig named(FEI_NAMED_SYSTEM(named_profile_system));
    FunctionSystem<decltype(named_profile_system)*> bare(named_profile_system);

    REQUIRE(named.profile.name == "named_profile_system");
    REQUIRE(named.profile.named());
    REQUIRE(named.system->hashable());
    REQUIRE(named.system->hash() == hash_system(named_profile_system));
    REQUIRE(
        named.system->access().read_components == bare.access().read_components
    );
    REQUIRE(
        named.system->access().write_components ==
        bare.access().write_components
    );
}

TEST_CASE(
    "ECS system profile registry symbolizes function pointers",
    "[ecs][system][profile]"
) {
#if defined(_WIN32)
    auto profile = SystemProfileRegistry::instance().symbolize(
        hash_system(named_profile_system)
    );

    REQUIRE(profile.has_value());
    INFO("symbol function: " << profile->function);
    INFO("symbol name: " << profile->name);
    INFO("symbol file: " << profile->file);
    REQUIRE(
        profile->function.find("named_profile_system") != std::string::npos
    );
    REQUIRE(profile->function.find("ILT+") == std::string::npos);
    REQUIRE(profile->name == "named_profile_system");

    auto template_profile = SystemProfileRegistry::instance().symbolize(
        hash_system(template_profile_system<Position>)
    );
    REQUIRE(template_profile.has_value());
    INFO("template symbol function: " << template_profile->function);
    INFO("template symbol name: " << template_profile->name);
    REQUIRE(
        template_profile->function.find("template_profile_system") !=
        std::string::npos
    );
    REQUIRE(template_profile->name == "template_profile_system");

    auto template_static_profile = SystemProfileRegistry::instance().symbolize(
        hash_system(TemplateProfileSystems<Position>::static_profile_system)
    );
    REQUIRE(template_static_profile.has_value());
    INFO(
        "template static symbol function: "
        << template_static_profile->function
    );
    INFO("template static symbol name: " << template_static_profile->name);
    REQUIRE(
        template_static_profile->function.find("static_profile_system") !=
        std::string::npos
    );
    REQUIRE(template_static_profile->name == "static_profile_system");
#else
    SUCCEED("Function pointer symbolization is only implemented on Windows");
#endif
}

TEST_CASE("ECS const queries expose read-only components", "[ecs][query]") {
    register_components();
    World world;
    auto entity = world.entity();
    world.add_component(entity, Position(2.0f, 3.0f));

    float x = 0.0f;
    world.run_system_once([&x](Query<const Position> query) {
        for (auto [pos] : query) {
            static_assert(
                std::is_const_v<std::remove_reference_t<decltype(pos)>>
            );
            x += pos.x;
        }
    });

    REQUIRE(x == 2.0f);
}

TEST_CASE("ECS system access metadata detects conflicts", "[ecs][system]") {
    FunctionSystem<decltype(read_position_system)*> read_position(
        read_position_system
    );
    FunctionSystem<decltype(write_position_system)*> write_position(
        write_position_system
    );
    FunctionSystem<decltype(resource_access_system)*> resource_access(
        resource_access_system
    );
    FunctionSystem<decltype(commands_system)*> commands(commands_system);

    REQUIRE(read_position.access().conflicts_with(write_position.access()));
    REQUIRE_FALSE(
        read_position.access().conflicts_with(resource_access.access())
    );
    REQUIRE(commands.access().conflicts_with(read_position.access()));
    REQUIRE(commands.access().is_barrier());
}

TEST_CASE(
    "ECS schedule batches systems by access compatibility",
    "[ecs][schedule]"
) {
    SECTION("Compatible systems share a batch") {
        Schedule schedule;
        schedule.add_systems(read_position_system, read_velocity_system);
        schedule.sort_systems();

        REQUIRE(schedule.execution_batches().size() == 1);
        REQUIRE(schedule.execution_batches().front().size() == 2);
    }

    SECTION("Conflicting systems are split across batches") {
        Schedule schedule;
        schedule.add_systems(read_position_system, write_position_system);
        schedule.sort_systems();

        REQUIRE(schedule.execution_batches().size() == 2);
        REQUIRE(schedule.execution_batches()[0].size() == 1);
        REQUIRE(schedule.execution_batches()[1].size() == 1);
    }

    SECTION("Explicit dependencies split otherwise compatible systems") {
        Schedule schedule;
        schedule.add_systems(
            chain(read_position_system, read_position_again_system)
        );
        schedule.sort_systems();

        REQUIRE(schedule.execution_batches().size() == 2);
        REQUIRE(schedule.execution_batches()[0].size() == 1);
        REQUIRE(schedule.execution_batches()[1].size() == 1);
    }
}

TEST_CASE("ECS named lambda systems run", "[ecs][system]") {
    World world;
    world.add_resource(CommandsQueue {});

    int calls = 0;
    Schedule schedule;
    schedule.add_systems(FEI_SYSTEM_NAME("named_lambda", [&calls]() {
        ++calls;
    }));
    schedule.sort_systems();
    schedule.run_systems(world);

    REQUIRE(calls == 1);
}

TEST_CASE(
    "ECS schedule runs compatible systems in parallel",
    "[ecs][schedule]"
) {
    World world;
    world.set_worker_threads(2);
    world.add_resource(CommandsQueue {});

    std::mutex mutex;
    std::condition_variable cv;
    int entered = 0;
    int overlapped = 0;

    auto wait_for_peer = [&]() {
        bool saw_peer = false;
        {
            std::unique_lock lock(mutex);
            ++entered;
            if (entered == 2) {
                saw_peer = true;
                cv.notify_all();
            } else {
                saw_peer = cv.wait_for(lock, std::chrono::seconds {1}, [&]() {
                    return entered == 2;
                });
            }
        }

        if (saw_peer) {
            std::scoped_lock lock(mutex);
            ++overlapped;
        }
    };

    world.add_systems(
        TestSchedule,
        [&](Query<const Position>) {
            wait_for_peer();
        },
        [&](Query<const Velocity>) {
            wait_for_peer();
        }
    );
    world.sort_systems();
    world.run_schedule(TestSchedule);

    REQUIRE(world.worker_threads() == 2);
    REQUIRE(overlapped == 2);
}

TEST_CASE(
    "ECS exclusive params are represented in access metadata",
    "[ecs][system]"
) {
    FunctionSystem<decltype(world_ref_system)*> world_ref(world_ref_system);
    FunctionSystem<decltype(event_access_system)*> event_access(
        event_access_system
    );
    FunctionSystem<decltype(custom_param_system)*> custom_param(
        custom_param_system
    );

    REQUIRE(world_ref.access().world_exclusive);
    REQUIRE(world_ref.access().is_barrier());
    REQUIRE(event_access.access().write_resources.contains(
        type_id<Events<GameEvent>>()
    ));
    REQUIRE(event_access.access().write_resources.contains(
        type_id<Events<PlayerMoved>>()
    ));
    REQUIRE(custom_param.access().world_exclusive);
}

TEST_CASE(
    "ECS main-thread-only resource traits create scheduler barriers",
    "[ecs][system]"
) {
    FunctionSystem<decltype(main_thread_resource_system)*> main_thread(
        main_thread_resource_system
    );
    FunctionSystem<decltype(read_position_system)*> read_position(
        read_position_system
    );

    REQUIRE(main_thread.access().main_thread_only);
    REQUIRE_FALSE(main_thread.access().world_exclusive);
    REQUIRE(main_thread.access().is_barrier());
    REQUIRE(main_thread.access().conflicts_with(read_position.access()));

    Schedule schedule;
    schedule.add_systems(main_thread_resource_system, read_position_system);
    schedule.sort_systems();

    REQUIRE(schedule.execution_batches().size() == 2);
    REQUIRE(schedule.execution_batches()[0].size() == 1);
    REQUIRE(schedule.execution_batches()[1].size() == 1);
}
