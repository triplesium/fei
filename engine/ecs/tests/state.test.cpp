#include "ecs/state.hpp"

#include "test_types.hpp"

#include <catch2/catch_test_macros.hpp>
#include <functional>
#include <string>
#include <vector>

namespace state_test_types {

struct CollidingState {
    int value {0};

    bool operator==(const CollidingState&) const = default;
};

} // namespace state_test_types

template<>
struct std::hash<state_test_types::CollidingState> {
    std::size_t operator()(const state_test_types::CollidingState&) const {
        return 0;
    }
};

using namespace fei;
using namespace fei::ecs_test;
using state_test_types::CollidingState;

namespace {

enum class GameplayState {
    Loading,
    Playing,
    Paused,
};

enum class OverlayState {
    Hidden,
    Visible,
};

void request_playing(ResRW<NextState<GameplayState>> next_state) {
    next_state->set(GameplayState::Playing);
}

void request_paused(ResRW<NextState<GameplayState>> next_state) {
    next_state->set(GameplayState::Paused);
}

void exit_loading(
    ResRO<State<GameplayState>> state,
    ResRW<ScheduleTrace> trace
) {
    trace->entries.emplace_back(
        state->get() == GameplayState::Playing ? "exit:loading:new" :
                                                 "exit:loading:old"
    );
}

void enter_playing(
    ResRO<State<GameplayState>> state,
    ResRW<ScheduleTrace> trace
) {
    trace->entries.emplace_back(
        state->get() == GameplayState::Playing ? "enter:playing:new" :
                                                 "enter:playing:old"
    );
}

void loading_to_playing(
    ResRO<State<GameplayState>> state,
    ResRW<ScheduleTrace> trace
) {
    trace->entries.emplace_back(
        state->get() == GameplayState::Playing ?
            "transition:loading-playing:new" :
            "transition:loading-playing:old"
    );
}

void exit_loading_observes_overlay(
    ResRO<State<OverlayState>> overlay,
    ResRW<ScheduleTrace> trace
) {
    trace->entries.emplace_back(
        overlay->get() == OverlayState::Visible ? "overlay:new" : "overlay:old"
    );
}

void queue_overlay_visible(ResRW<NextState<OverlayState>> next_state) {
    next_state->set(OverlayState::Visible);
}

} // namespace

TEST_CASE("ECS in_state conditions read the current state", "[ecs][state]") {
    World world;
    world.add_resource(ScheduleTrace {});
    world.init_state(GameplayState::Loading);
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
    world.resource<ScheduleTrace>().entries.clear();
    world.run_schedule(TestSchedule);
    REQUIRE(
        world.resource<ScheduleTrace>().entries ==
        std::vector<std::string> {"first"}
    );

    world.run_state_transitions();
    world.run_state_transitions();
    world.resource<ScheduleTrace>().entries.clear();
    world.run_schedule(TestSchedule);
    REQUIRE(
        world.resource<ScheduleTrace>().entries ==
        std::vector<std::string> {"second"}
    );
}

TEST_CASE("ECS in_state returns false when state is missing", "[ecs][state]") {
    World world;
    world.add_resource(CommandsQueue {});
    world.add_resource(ScheduleTrace {});
    world.add_systems(
        TestSchedule,
        scheduled_first | run_if(in_state(GameplayState::Loading))
    );
    world.sort_systems();

    world.run_schedule(TestSchedule);

    REQUIRE(world.resource<ScheduleTrace>().entries.empty());
}

TEST_CASE("ECS init_state is idempotent", "[ecs][state]") {
    World world;
    world.init_state(GameplayState::Loading);
    world.resource<NextState<GameplayState>>().set(GameplayState::Playing);

    auto& state = world.init_state(GameplayState::Paused);

    REQUIRE(state.get() == GameplayState::Loading);
    REQUIRE(
        world.resource<NextState<GameplayState>>().pending() ==
        Optional<GameplayState> {GameplayState::Playing}
    );
}

TEST_CASE("ECS insert_state explicitly replaces state", "[ecs][state]") {
    World world;
    world.add_resource(ScheduleTrace {});
    world.init_state(GameplayState::Loading);
    world.resource<NextState<GameplayState>>().set(GameplayState::Playing);
    world.insert_state(GameplayState::Paused);
    world.add_systems(
        OnEnter(GameplayState::Paused),
        [](ResRW<ScheduleTrace> trace) {
            trace->entries.emplace_back("enter:paused");
        }
    );
    world.sort_systems();

    world.run_state_transitions();

    REQUIRE(
        world.resource<State<GameplayState>>().get() == GameplayState::Paused
    );
    REQUIRE_FALSE(world.resource<NextState<GameplayState>>().has_value());
    REQUIRE(
        world.resource<ScheduleTrace>().entries ==
        std::vector<std::string> {"enter:paused"}
    );
}

TEST_CASE(
    "ECS state transitions run exit transition and enter phases",
    "[ecs][state]"
) {
    World world;
    world.add_resource(ScheduleTrace {});
    world.init_state(GameplayState::Loading);
    world.add_systems(OnExit(GameplayState::Loading), exit_loading);
    world.add_systems(
        OnTransition(GameplayState::Loading, GameplayState::Playing),
        loading_to_playing
    );
    world.add_systems(OnEnter(GameplayState::Playing), enter_playing);
    world.sort_systems();
    world.run_state_transitions();

    world.run_system_once(request_playing);
    world.run_state_transitions();

    REQUIRE(
        world.resource<ScheduleTrace>().entries ==
        std::vector<std::string> {
            "exit:loading:new",
            "transition:loading-playing:new",
            "enter:playing:new"
        }
    );
}

TEST_CASE(
    "ECS applies every state before running transition callbacks",
    "[ecs][state]"
) {
    World world;
    world.add_resource(ScheduleTrace {});
    world.init_state(GameplayState::Loading);
    world.init_state(OverlayState::Hidden);
    world.add_systems(
        OnExit(GameplayState::Loading),
        exit_loading_observes_overlay
    );
    world.sort_systems();
    world.run_state_transitions();

    world.resource<NextState<GameplayState>>().set(GameplayState::Playing);
    world.resource<NextState<OverlayState>>().set(OverlayState::Visible);
    world.run_state_transitions();

    REQUIRE(
        world.resource<ScheduleTrace>().entries ==
        std::vector<std::string> {"overlay:new"}
    );
}

TEST_CASE(
    "ECS defers state requests made by transition callbacks",
    "[ecs][state]"
) {
    World world;
    world.init_state(GameplayState::Loading);
    world.init_state(OverlayState::Hidden);
    world.add_systems(OnEnter(GameplayState::Playing), queue_overlay_visible);
    world.sort_systems();
    world.run_state_transitions();

    world.resource<NextState<GameplayState>>().set(GameplayState::Playing);
    world.run_state_transitions();

    REQUIRE(
        world.resource<State<OverlayState>>().get() == OverlayState::Hidden
    );
    REQUIRE(world.resource<NextState<OverlayState>>().has_value());

    world.run_state_transitions();
    REQUIRE(
        world.resource<State<OverlayState>>().get() == OverlayState::Visible
    );
}

TEST_CASE("ECS skips unchanged state requests", "[ecs][state]") {
    World world;
    world.add_resource(ScheduleTrace {});
    world.init_state(GameplayState::Loading);
    world.add_systems(OnExit(GameplayState::Loading), exit_loading);
    world.sort_systems();
    world.run_state_transitions();

    world.resource<NextState<GameplayState>>().set(GameplayState::Loading);
    world.run_state_transitions();

    REQUIRE(world.resource<ScheduleTrace>().entries.empty());
    REQUIRE_FALSE(world.resource<NextState<GameplayState>>().has_value());
}

TEST_CASE(
    "ECS applies the latest pending state and clears it",
    "[ecs][state]"
) {
    World world;
    world.init_state(GameplayState::Loading);
    world.sort_systems();
    world.run_state_transitions();

    world.run_system_once(request_playing);
    world.run_system_once(request_paused);
    world.run_state_transitions();

    REQUIRE(
        world.resource<State<GameplayState>>().get() == GameplayState::Paused
    );
    REQUIRE_FALSE(world.resource<NextState<GameplayState>>().has_value());
}

TEST_CASE("ECS state schedules distinguish colliding hashes", "[ecs][state]") {
    const CollidingState one {.value = 1};
    const CollidingState two {.value = 2};

    REQUIRE(OnEnter(one).id() != OnEnter(two).id());
    REQUIRE(OnExit(one).id() != OnExit(two).id());
    REQUIRE(OnTransition(one, two).id() != OnTransition(two, one).id());
    REQUIRE(OnEnter(one).id() != OnExit(one).id());

    World world;
    world.add_resource(ScheduleTrace {});
    world.init_state(one);
    world.add_systems(OnEnter(one), [](ResRW<ScheduleTrace> trace) {
        trace->entries.emplace_back("one");
    });
    world.add_systems(OnEnter(two), [](ResRW<ScheduleTrace> trace) {
        trace->entries.emplace_back("two");
    });
    world.sort_systems();

    world.run_state_transitions();
    world.resource<NextState<CollidingState>>().set(two);
    world.run_state_transitions();

    REQUIRE(
        world.resource<ScheduleTrace>().entries ==
        std::vector<std::string> {"one", "two"}
    );
}
