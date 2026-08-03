#include "ecs/commands.hpp"
#include "ecs/schedule.hpp"
#include "ecs/system_config.hpp"
#include "ecs/system_params.hpp"
#include "ecs/system_profile.hpp"
#include "ecs/world.hpp"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
#include <exception>
#include <string_view>
#include <utility>
#include <vector>

#ifdef _MSC_VER
#    include <process.h>
#endif

using namespace fei;

namespace {

constexpr ScheduleId DebugSchedule = 91;
constexpr int DependencyFatalExitCode = 86;

void shadow_debug_system() {}
void lighting_debug_system() {}

struct ExecutionOrder {
    std::vector<int> values;
};

void first_debug_system(ResRW<ExecutionOrder> order) {
    order->values.push_back(1);
}

void second_debug_system(ResRW<ExecutionOrder> order) {
    order->values.push_back(2);
}

void third_debug_system(ResRW<ExecutionOrder> order) {
    order->values.push_back(3);
}

void repeated_debug_system(ResRW<ExecutionOrder> order) {
    order->values.push_back(7);
}

template<typename T>
concept CanCreateAfterTag =
    requires(T&& target) { after(std::forward<T>(target)); };

static_assert(CanCreateAfterTag<SystemConfig&>);
static_assert(!CanCreateAfterTag<decltype(repeated_debug_system)*>);

const SystemScheduleDebugInfo&
find_system(const ScheduleDebugInfo& debug, SystemId id) {
    auto it =
        std::ranges::find(debug.systems, id, &SystemScheduleDebugInfo::id);
    REQUIRE(it != debug.systems.end());
    return *it;
}

int run_test_in_subprocess(const char* test_name) {
#ifdef _MSC_VER
    char* executable = nullptr;
    if (_get_pgmptr(&executable) != 0 || executable == nullptr) {
        return -1;
    }
    return static_cast<int>(_spawnl(
        _P_WAIT,
        executable,
        executable,
        test_name,
        static_cast<char*>(nullptr)
    ));
#else
    (void)test_name;
    return -1;
#endif
}

void install_dependency_fatal_test_handler() {
    std::set_terminate([]() {
        std::_Exit(DependencyFatalExitCode);
    });
}

} // namespace

TEST_CASE(
    "Schedule debug info exposes named explicit dependencies and batches",
    "[ecs][schedule][debug]"
) {
    World world;
    world.add_systems(
        DebugSchedule,
        chain(
            FEI_NAMED_SYSTEM(shadow_debug_system),
            FEI_NAMED_SYSTEM(lighting_debug_system)
        )
    );

    auto debug = world.schedule_debug_info(DebugSchedule);
    REQUIRE(debug);
    REQUIRE(debug->systems.size() == 2);
    REQUIRE(debug->batches.size() == 2);
    REQUIRE(debug->systems[0].name == "shadow_debug_system");
    REQUIRE(debug->systems[1].name == "lighting_debug_system");
    REQUIRE(
        debug->systems[1].dependencies ==
        std::vector<SystemId> {debug->systems[0].id}
    );
    REQUIRE(debug->systems[0].topological_index == 0);
    REQUIRE(debug->systems[1].topological_index == 1);
    REQUIRE(debug->systems[0].batch_index == 0);
    REQUIRE(debug->systems[1].batch_index == 1);
}

TEST_CASE(
    "Named three-system chain preserves instance dependencies and execution",
    "[ecs][schedule][debug][chain]"
) {
    World world;
    world.add_resource(ExecutionOrder {});
    world.add_resource(CommandsQueue {});
    world.add_systems(
        DebugSchedule,
        chain(
            FEI_NAMED_SYSTEM(first_debug_system),
            FEI_NAMED_SYSTEM(second_debug_system),
            FEI_NAMED_SYSTEM(third_debug_system)
        )
    );

    auto debug = world.schedule_debug_info(DebugSchedule);
    REQUIRE(debug);
    REQUIRE(debug->systems.size() == 3);

    auto find_system = [&](std::string_view name) -> const auto& {
        auto it = std::ranges::find(
            debug->systems,
            name,
            &SystemScheduleDebugInfo::name
        );
        REQUIRE(it != debug->systems.end());
        return *it;
    };
    const auto& first = find_system("first_debug_system");
    const auto& second = find_system("second_debug_system");
    const auto& third = find_system("third_debug_system");
    CHECK(second.dependencies == std::vector<SystemId> {first.id});
    CHECK(third.dependencies == std::vector<SystemId> {second.id});
    CHECK(first.topological_index < second.topological_index);
    CHECK(second.topological_index < third.topological_index);
    CHECK(first.batch_index < second.batch_index);
    CHECK(second.batch_index < third.batch_index);

    world.run_schedule(DebugSchedule);
    CHECK(world.resource<ExecutionOrder>().values == std::vector {1, 2, 3});
}

TEST_CASE(
    "The same system function can be registered as independent instances",
    "[ecs][schedule][debug][instance]"
) {
    World world;
    world.add_systems(
        DebugSchedule,
        FEI_NAMED_SYSTEM(repeated_debug_system),
        FEI_NAMED_SYSTEM(repeated_debug_system)
    );

    auto debug = world.schedule_debug_info(DebugSchedule);
    REQUIRE(debug);
    REQUIRE(debug->systems.size() == 2);
    CHECK(debug->systems[0].id != debug->systems[1].id);
    CHECK(std::ranges::all_of(debug->systems, [](const auto& system) {
        return system.name == "repeated_debug_system";
    }));
    CHECK(debug->systems[0].dependencies.empty());
    CHECK(debug->systems[1].dependencies.empty());
}

TEST_CASE(
    "Explicit dependencies select one of repeated system instances",
    "[ecs][schedule][debug][instance]"
) {
    auto first = SystemConfig(FEI_NAMED_SYSTEM(repeated_debug_system));
    auto second = SystemConfig(FEI_NAMED_SYSTEM(repeated_debug_system));
    auto follower = SystemConfig(FEI_NAMED_SYSTEM(third_debug_system));
    const auto first_id = first.id;
    const auto second_id = second.id;
    const auto follower_id = follower.id;
    follower.after(second);

    Schedule schedule;
    schedule
        .add_systems(std::move(first), std::move(second), std::move(follower));

    auto debug = schedule.debug_info(DebugSchedule);
    CHECK(find_system(debug, first_id).dependencies.empty());
    CHECK(find_system(debug, second_id).dependencies.empty());
    CHECK(
        find_system(debug, follower_id).dependencies ==
        std::vector<SystemId> {second_id}
    );
}

TEST_CASE(
    "Replacing a dependency target preserves its system identity",
    "[ecs][schedule][debug][replace]"
) {
    auto target = SystemConfig(first_debug_system);
    auto dependent = SystemConfig(second_debug_system);
    const auto target_id = target.id;
    const auto dependent_id = dependent.id;
    dependent.after(target);

    Schedule schedule;
    schedule.add_systems(std::move(target), std::move(dependent));
    auto replacement = SystemConfig(third_debug_system);
    REQUIRE(schedule.replace_system(target_id, std::move(replacement)));

    auto debug = schedule.debug_info(DebugSchedule);
    CHECK(
        find_system(debug, dependent_id).dependencies ==
        std::vector<SystemId> {target_id}
    );

    World world;
    world.add_resource(ExecutionOrder {});
    world.add_resource(CommandsQueue {});
    schedule.run_systems(world);
    CHECK(world.resource<ExecutionOrder>().values == std::vector {3, 2});
}

TEST_CASE(
    "The same system function can be registered in different schedules",
    "[ecs][schedule][debug][unique]"
) {
    constexpr ScheduleId other_schedule = 92;
    World world;
    world.add_systems(DebugSchedule, FEI_NAMED_SYSTEM(shadow_debug_system));
    world.add_systems(other_schedule, FEI_NAMED_SYSTEM(shadow_debug_system));

    auto first = world.schedule_debug_info(DebugSchedule);
    auto second = world.schedule_debug_info(other_schedule);
    REQUIRE(first);
    REQUIRE(second);
    REQUIRE(first->systems.size() == 1);
    REQUIRE(second->systems.size() == 1);
    CHECK(first->systems[0].name == "shadow_debug_system");
    CHECK(second->systems[0].name == "shadow_debug_system");
}

TEST_CASE(
    "The same system function can be chained as distinct instances",
    "[ecs][schedule][debug][chain]"
) {
    World world;
    world.add_resource(ExecutionOrder {});
    world.add_resource(CommandsQueue {});
    world.add_systems(
        DebugSchedule,
        chain(
            FEI_NAMED_SYSTEM(repeated_debug_system),
            FEI_NAMED_SYSTEM(repeated_debug_system)
        )
    );

    auto debug = world.schedule_debug_info(DebugSchedule);
    REQUIRE(debug);
    REQUIRE(debug->systems.size() == 2);
    CHECK(debug->systems[0].id != debug->systems[1].id);
    CHECK(debug->systems[0].name == "repeated_debug_system");
    CHECK(debug->systems[1].name == "repeated_debug_system");
    CHECK(
        debug->systems[1].dependencies ==
        std::vector<SystemId> {debug->systems[0].id}
    );

    world.run_schedule(DebugSchedule);
    CHECK(world.resource<ExecutionOrder>().values == std::vector {7, 7});
}

TEST_CASE(
    "Missing explicit dependency target terminates schedule construction",
    "[.dependency-missing][ecs][schedule][dependency-child]"
) {
    install_dependency_fatal_test_handler();
    auto target = SystemConfig(first_debug_system);
    auto dependent = SystemConfig(second_debug_system);
    dependent.after(target);

    Schedule schedule;
    schedule.add_system(std::move(dependent));
    schedule.sort_systems();
}

TEST_CASE(
    "Removing an explicit dependency target terminates schedule construction",
    "[.dependency-removed][ecs][schedule][dependency-child]"
) {
    install_dependency_fatal_test_handler();
    auto target = SystemConfig(first_debug_system);
    auto dependent = SystemConfig(second_debug_system);
    const auto target_id = target.id;
    dependent.after(target);

    Schedule schedule;
    schedule.add_systems(std::move(target), std::move(dependent));
    REQUIRE(schedule.remove_system(target_id));
    schedule.sort_systems();
}

TEST_CASE(
    "Invalid explicit dependency targets fail explicitly",
    "[ecs][schedule][dependency]"
) {
#ifdef _MSC_VER
    CHECK(
        run_test_in_subprocess("[.dependency-missing]") ==
        DependencyFatalExitCode
    );
    CHECK(
        run_test_in_subprocess("[.dependency-removed]") ==
        DependencyFatalExitCode
    );
#else
    SKIP("Schedule death tests are currently implemented for MSVC");
#endif
}
