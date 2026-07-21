#include "ecs/commands.hpp"
#include "ecs/system_params.hpp"
#include "ecs/world.hpp"
#include "refl/registry.hpp"

#include <catch2/catch_test_macros.hpp>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

using namespace fei;

namespace {

constexpr ScheduleId RegisteredSystemTestSchedule = 991;

struct RegisteredCounter {
    int value {0};
};

struct RegisteredTrace {
    std::vector<std::string> entries;
};

struct DeferredRegisteredValue {
    int value {0};
};

} // namespace

TEST_CASE(
    "ECS registered systems preserve state between runs",
    "[ecs][registered_system]"
) {
    Registry::instance().register_type<RegisteredCounter>();

    World world;
    world.add_resource(RegisteredCounter {});

    std::vector<bool> changed;
    auto id =
        world.register_system([&changed](ResRO<RegisteredCounter> counter) {
            changed.push_back(counter.is_changed());
        });

    REQUIRE(world.run_system(id));
    REQUIRE(world.run_system(id));
    world.resource<RegisteredCounter>().value = 1;
    REQUIRE(world.run_system(id));

    REQUIRE(changed == std::vector<bool> {true, false, true});
}

TEST_CASE(
    "ECS registered system ids report invalid and active operations",
    "[ecs][registered_system]"
) {
    World world;

    RegisteredSystemId id {};
    std::optional<RegisteredSystemError> recursive_error;
    std::optional<RegisteredSystemError> unregister_error;
    id = world.register_system([&](WorldRef world_ref) {
        auto recursive = world_ref->run_system(id);
        REQUIRE_FALSE(recursive);
        recursive_error = recursive.error();

        auto unregister = world_ref->unregister_system(id);
        REQUIRE_FALSE(unregister);
        unregister_error = unregister.error();
    });

    REQUIRE(world.run_system(id));
    REQUIRE(recursive_error == RegisteredSystemError::AlreadyRunning);
    REQUIRE(unregister_error == RegisteredSystemError::AlreadyRunning);

    REQUIRE(world.unregister_system(id));
    auto missing_run = world.run_system(id);
    REQUIRE_FALSE(missing_run);
    REQUIRE(missing_run.error() == RegisteredSystemError::NotFound);

    auto missing_unregister = world.unregister_system(id);
    REQUIRE_FALSE(missing_unregister);
    REQUIRE(missing_unregister.error() == RegisteredSystemError::NotFound);
}

TEST_CASE(
    "ECS registered systems recover their running state after exceptions",
    "[ecs][registered_system]"
) {
    World world;
    bool should_throw = true;
    int completed_runs = 0;
    auto id = world.register_system([&]() {
        if (should_throw) {
            should_throw = false;
            throw std::runtime_error("registered system failure");
        }
        ++completed_runs;
    });

    REQUIRE_THROWS_AS(world.run_system(id), std::runtime_error);
    REQUIRE(world.run_system(id));
    REQUIRE(completed_runs == 1);
}

TEST_CASE(
    "ECS commands run and unregister registered systems after a batch",
    "[ecs][commands][registered_system]"
) {
    Registry::instance().register_type<CommandsQueue>();
    Registry::instance().register_type<RegisteredTrace>();
    Registry::instance().register_type<DeferredRegisteredValue>();

    World world;
    world.add_resource(CommandsQueue {});
    world.add_resource(RegisteredTrace {});

    auto registered = world.register_system([](Commands commands,
                                               ResRW<RegisteredTrace> trace) {
        trace->entries.emplace_back("registered");
        commands.add_resource(DeferredRegisteredValue {.value = 42});
    });

    auto trigger = SystemConfig(
        [registered](Commands commands, ResRW<RegisteredTrace> trace) {
            trace->entries.emplace_back("trigger");
            commands.run_system(registered);
            commands.unregister_system(registered);
        }
    );
    auto observe = SystemConfig([](ResRO<DeferredRegisteredValue> value,
                                   ResRW<RegisteredTrace> trace) {
        trace->entries.push_back("observe:" + std::to_string(value->value));
    });
    observe.after(trigger);
    world.add_systems(
        RegisteredSystemTestSchedule,
        std::move(trigger),
        std::move(observe)
    );

    world.run_schedule(RegisteredSystemTestSchedule);

    REQUIRE(
        world.resource<RegisteredTrace>().entries ==
        std::vector<std::string> {"trigger", "registered", "observe:42"}
    );
    auto removed = world.run_system(registered);
    REQUIRE_FALSE(removed);
    REQUIRE(removed.error() == RegisteredSystemError::NotFound);
}

TEST_CASE(
    "ECS immediate registered systems flush ordinary commands",
    "[ecs][commands][registered_system]"
) {
    Registry::instance().register_type<CommandsQueue>();
    Registry::instance().register_type<DeferredRegisteredValue>();

    World world;
    world.add_resource(CommandsQueue {});

    auto id = world.register_system([](Commands commands) {
        commands.add_resource(DeferredRegisteredValue {.value = 7});
    });

    REQUIRE(world.run_system(id));
    REQUIRE(world.resource<DeferredRegisteredValue>().value == 7);
}
