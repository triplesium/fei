#include "ecs/state.hpp"

#include "test_types.hpp"

#include <catch2/catch_test_macros.hpp>
#include <string>
#include <vector>

using namespace fei;
using namespace fei::ecs_test;

namespace {

enum class GameplayState {
    Loading,
    Playing,
    Paused,
};

void request_playing(ResRW<NextState<GameplayState>> next_state) {
    next_state->set(GameplayState::Playing);
}

void request_paused(ResRW<NextState<GameplayState>> next_state) {
    next_state->set(GameplayState::Paused);
}

void request_loading(ResRW<NextState<GameplayState>> next_state) {
    next_state->set(GameplayState::Loading);
}

void exit_loading(
    ResRO<State<GameplayState>> state,
    ResRW<ScheduleTrace> trace
) {
    trace->entries.push_back(
        state->get() == GameplayState::Loading ? "exit:loading:old" :
                                                 "exit:loading:new"
    );
}

void enter_playing(
    ResRO<State<GameplayState>> state,
    ResRW<ScheduleTrace> trace
) {
    trace->entries.push_back(
        state->get() == GameplayState::Playing ? "enter:playing:new" :
                                                 "enter:playing:old"
    );
}

} // namespace

TEST_CASE("ECS in_state conditions read the current state", "[ecs][state]") {
    Registry::instance().register_type<CommandsQueue>();
    Registry::instance().register_type<ScheduleTrace>();
    Registry::instance().register_type<State<GameplayState>>();
    Registry::instance().register_type<NextState<GameplayState>>();

    World world;
    world.add_resource(CommandsQueue {});
    world.add_resource(ScheduleTrace {});
    world.add_resource(State<GameplayState> {GameplayState::Loading});
    world.add_resource(NextState<GameplayState> {});

    world.add_systems(
        TestSchedule,
        chain(
            scheduled_first | run_if(in_state(GameplayState::Loading)),
            scheduled_second | run_if(in_state(GameplayState::Playing))
        )
    );
    world.sort_systems();

    world.run_schedule(TestSchedule);
    REQUIRE(
        world.resource<ScheduleTrace>().entries ==
        std::vector<std::string> {"first"}
    );

    world.run_system_once(request_playing);
    REQUIRE(
        world.resource<State<GameplayState>>().get() == GameplayState::Loading
    );

    world.resource<ScheduleTrace>().entries.clear();
    world.run_schedule(TestSchedule);
    REQUIRE(
        world.resource<ScheduleTrace>().entries ==
        std::vector<std::string> {"first"}
    );

    world.run_system_once(apply_state_transition<GameplayState>);
    REQUIRE(
        world.resource<State<GameplayState>>().get() == GameplayState::Playing
    );

    world.resource<ScheduleTrace>().entries.clear();
    world.run_schedule(TestSchedule);
    REQUIRE(
        world.resource<ScheduleTrace>().entries ==
        std::vector<std::string> {"second"}
    );
}

TEST_CASE("ECS world initializes state transition schedules", "[ecs][state]") {
    Registry::instance().register_type<CommandsQueue>();
    Registry::instance().register_type<State<GameplayState>>();
    Registry::instance().register_type<NextState<GameplayState>>();

    World world;
    auto& state = world.init_state(GameplayState::Loading);

    REQUIRE(state.get() == GameplayState::Loading);
    REQUIRE(world.has_resource<State<GameplayState>>());
    REQUIRE(world.has_resource<NextState<GameplayState>>());
    REQUIRE(world.has_resource<CommandsQueue>());

    world.sort_systems();
    world.run_system_once(request_playing);
    world.run_state_transitions();

    REQUIRE(
        world.resource<State<GameplayState>>().get() == GameplayState::Playing
    );
    REQUIRE_FALSE(world.resource<NextState<GameplayState>>().has_value());
}

TEST_CASE(
    "ECS state transitions run enter and exit schedules",
    "[ecs][state]"
) {
    Registry::instance().register_type<CommandsQueue>();
    Registry::instance().register_type<ScheduleTrace>();
    Registry::instance().register_type<State<GameplayState>>();
    Registry::instance().register_type<NextState<GameplayState>>();

    World world;
    world.add_resource(CommandsQueue {});
    world.add_resource(ScheduleTrace {});
    world.add_resource(State<GameplayState> {GameplayState::Loading});
    world.add_resource(NextState<GameplayState> {});
    world.add_systems(on_exit(GameplayState::Loading), exit_loading);
    world.add_systems(on_enter(GameplayState::Playing), enter_playing);
    world.sort_systems();

    world.run_system_once(request_playing);
    world.run_system_once(apply_state_transition<GameplayState>);

    REQUIRE(
        world.resource<ScheduleTrace>().entries ==
        std::vector<std::string> {"exit:loading:old", "enter:playing:new"}
    );
    REQUIRE(
        world.resource<State<GameplayState>>().get() == GameplayState::Playing
    );
}

TEST_CASE("ECS state transitions skip unchanged states", "[ecs][state]") {
    Registry::instance().register_type<CommandsQueue>();
    Registry::instance().register_type<ScheduleTrace>();
    Registry::instance().register_type<State<GameplayState>>();
    Registry::instance().register_type<NextState<GameplayState>>();

    World world;
    world.add_resource(CommandsQueue {});
    world.add_resource(ScheduleTrace {});
    world.add_resource(State<GameplayState> {GameplayState::Loading});
    world.add_resource(NextState<GameplayState> {});
    world.add_systems(on_exit(GameplayState::Loading), exit_loading);
    world.sort_systems();

    world.run_system_once(request_loading);
    world.run_system_once(apply_state_transition<GameplayState>);

    REQUIRE(world.resource<ScheduleTrace>().entries.empty());
    REQUIRE(
        world.resource<State<GameplayState>>().get() == GameplayState::Loading
    );
    REQUIRE_FALSE(world.resource<NextState<GameplayState>>().has_value());
}

TEST_CASE(
    "ECS state transitions apply the latest pending state and clear it",
    "[ecs][state]"
) {
    World world;
    world.add_resource(State<GameplayState> {GameplayState::Loading});
    world.add_resource(NextState<GameplayState> {});

    world.run_system_once(request_playing);
    world.run_system_once(request_paused);

    REQUIRE(
        world.resource<State<GameplayState>>().get() == GameplayState::Loading
    );
    REQUIRE(world.resource<NextState<GameplayState>>().has_value());

    world.run_system_once(apply_state_transition<GameplayState>);

    REQUIRE(
        world.resource<State<GameplayState>>().get() == GameplayState::Paused
    );
    REQUIRE_FALSE(world.resource<NextState<GameplayState>>().has_value());

    world.run_system_once(apply_state_transition<GameplayState>);

    REQUIRE(
        world.resource<State<GameplayState>>().get() == GameplayState::Paused
    );
    REQUIRE_FALSE(world.resource<NextState<GameplayState>>().has_value());
}
