#include "app/sub_app.hpp"

#include "app/app.hpp"
#include "app/main_thread_executor.hpp"
#include "ecs/query.hpp"
#include "ecs/system_params.hpp"

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <condition_variable>
#include <future>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

using namespace fei;

namespace {

struct TestSubApp {};

struct MainValue {
    int value {0};
};

struct SubValue {
    int value {0};
};

struct SubTrace {
    std::vector<std::string> entries;
};

struct ThreadObservation {
    std::thread::id startup_thread;
    std::thread::id update_thread;
    std::thread::id platform_main_thread;
};

struct BlockingUpdateState {
    std::mutex mutex;
    std::condition_variable changed;
    bool started {false};
    bool released {false};
    bool finished {false};
};

class RecordingSubAppRunner final : public SubAppRunner {
  public:
    explicit RecordingSubAppRunner(
        SubApp sub_app,
        std::shared_ptr<std::vector<std::string>> trace
    ) : m_inline(std::move(sub_app)), m_trace(std::move(trace)) {}

    [[nodiscard]] SubAppExecutionMode execution_mode() const noexcept override {
        return m_inline.execution_mode();
    }

    SubApp& sub_app() override { return m_inline.sub_app(); }
    const SubApp& sub_app() const override { return m_inline.sub_app(); }

    void run_on_execution_thread(ExecutionTask task) override {
        m_inline.run_on_execution_thread(std::move(task));
    }

    void synchronize() override { m_inline.synchronize(); }

    void set_worker_threads(std::size_t thread_count) override {
        m_inline.set_worker_threads(thread_count);
    }

    void finish() override {
        m_trace->emplace_back("finish");
        m_inline.finish();
    }

    void startup(SubAppSource source) override {
        m_trace->emplace_back("startup");
        m_inline.startup(source);
    }

    void update(SubAppSource source) override {
        m_trace->emplace_back("update");
        m_inline.update(source);
    }

    void shutdown() noexcept override {
        m_trace->emplace_back("shutdown");
        m_inline.shutdown();
    }

  private:
    InlineSubAppRunner m_inline;
    std::shared_ptr<std::vector<std::string>> m_trace;
};

struct MainLifetimeProbe {
    std::shared_ptr<bool> alive;

    explicit MainLifetimeProbe(std::shared_ptr<bool> alive) :
        alive(std::move(alive)) {}
    MainLifetimeProbe(MainLifetimeProbe&& other) noexcept :
        alive(std::exchange(other.alive, nullptr)) {}
    MainLifetimeProbe& operator=(MainLifetimeProbe&&) = delete;
    MainLifetimeProbe(const MainLifetimeProbe&) = delete;
    MainLifetimeProbe& operator=(const MainLifetimeProbe&) = delete;
    ~MainLifetimeProbe() {
        if (alive) {
            *alive = false;
        }
    }
};

struct SubLifetimeProbe {
    std::shared_ptr<bool> main_alive;
    bool* destroyed_while_main_alive {nullptr};

    SubLifetimeProbe(
        std::shared_ptr<bool> main_alive,
        bool& destroyed_while_main_alive
    ) :
        main_alive(std::move(main_alive)),
        destroyed_while_main_alive(&destroyed_while_main_alive) {}
    SubLifetimeProbe(SubLifetimeProbe&& other) noexcept :
        main_alive(std::move(other.main_alive)),
        destroyed_while_main_alive(
            std::exchange(other.destroyed_while_main_alive, nullptr)
        ) {}
    SubLifetimeProbe& operator=(SubLifetimeProbe&&) = delete;
    SubLifetimeProbe(const SubLifetimeProbe&) = delete;
    SubLifetimeProbe& operator=(const SubLifetimeProbe&) = delete;
    ~SubLifetimeProbe() {
        if (destroyed_while_main_alive) {
            *destroyed_while_main_alive = *main_alive;
        }
    }
};

struct WorkerLifetimeState {
    std::thread::id created_on;
    std::thread::id destroyed_on;
    std::thread::id platform_cleanup_on;
};

struct WorkerLifetimeProbe {
    std::shared_ptr<WorkerLifetimeState> state;
    const MainThreadExecutor* main_thread_executor {nullptr};

    WorkerLifetimeProbe(
        std::shared_ptr<WorkerLifetimeState> state,
        const MainThreadExecutor& main_thread_executor
    ) : state(std::move(state)), main_thread_executor(&main_thread_executor) {
        this->state->created_on = std::this_thread::get_id();
    }
    WorkerLifetimeProbe(WorkerLifetimeProbe&& other) noexcept :
        state(std::move(other.state)),
        main_thread_executor(
            std::exchange(other.main_thread_executor, nullptr)
        ) {}
    WorkerLifetimeProbe& operator=(WorkerLifetimeProbe&&) = delete;
    WorkerLifetimeProbe(const WorkerLifetimeProbe&) = delete;
    WorkerLifetimeProbe& operator=(const WorkerLifetimeProbe&) = delete;
    ~WorkerLifetimeProbe() {
        if (state) {
            state->destroyed_on = std::this_thread::get_id();
            state->platform_cleanup_on = main_thread_executor->execute([]() {
                return std::this_thread::get_id();
            });
        }
    }
};

constexpr ScheduleId SubStartup = 1000;
constexpr ScheduleId SubUpdate = 1001;

void sub_startup(ResRW<SubTrace> trace) {
    trace->entries.emplace_back("startup");
}

void sub_update(ResRW<SubValue> value, ResRW<SubTrace> trace) {
    ++value->value;
    trace->entries.emplace_back("update");
}

void observe_thread_startup(ResRW<ThreadObservation> observation) {
    observation->startup_thread = std::this_thread::get_id();
}

void execute_on_platform_main_thread(
    ResRO<MainThreadExecutor> executor,
    ResRW<ThreadObservation> observation
) {
    observation->platform_main_thread = executor->execute([]() {
        return std::this_thread::get_id();
    });
}

void observe_thread_update(ResRW<ThreadObservation> observation) {
    observation->update_thread = std::this_thread::get_id();
}

} // namespace

TEST_CASE("App owns and runs isolated sub apps", "[app][sub-app]") {
    App app;
    app.add_resource(MainValue {.value = 7});

    SubApp sub_app;
    sub_app.add_startup_schedule(SubStartup)
        .add_update_schedule(SubUpdate)
        .add_resource(SubValue {})
        .add_resource(SubTrace {})
        .add_systems(SubStartup, sub_startup)
        .add_systems(SubUpdate, sub_update)
        .add_extract([](World& main, World& sub) {
            sub.resource<SubValue>().value =
                static_cast<const World&>(main).resource<MainValue>().value;
        });
    app.insert_sub_app<TestSubApp>(std::move(sub_app));

    REQUIRE(app.has_sub_app<TestSubApp>());
    REQUIRE(
        app.sub_app_runner<TestSubApp>().execution_mode() ==
        SubAppExecutionMode::Inline
    );
    REQUIRE_FALSE(app.sub_app<TestSubApp>().world().has_resource<MainValue>());

    app.render();
    REQUIRE(app.sub_app<TestSubApp>().resource<SubValue>().value == 8);
    REQUIRE(
        app.sub_app<TestSubApp>().resource<SubTrace>().entries ==
        std::vector<std::string> {"startup", "update"}
    );

    app.resource<MainValue>().value = 12;
    app.render();
    REQUIRE(app.sub_app<TestSubApp>().resource<SubValue>().value == 13);
    REQUIRE(
        app.sub_app<TestSubApp>().resource<SubTrace>().entries ==
        std::vector<std::string> {"startup", "update", "update"}
    );
}

TEST_CASE(
    "App selects a SubApp source world for extraction and output",
    "[app][sub-app][source]"
) {
    struct SourceOutput {
        int value {0};
    };
    struct SourceTrace {
        std::vector<SubAppSourceContext> contexts;
    };

    App app;
    app.add_resource(MainValue {.value = 7});

    World alternate_world;
    alternate_world.add_resource(MainValue {.value = 19});
    bool use_alternate = false;

    SubApp sub_app;
    sub_app.add_resource(SubValue {})
        .add_resource(SourceTrace {})
        .set_pre_extract([](World&, World& sub) {
            sub.resource<SourceTrace>().contexts.push_back(
                sub.resource<SubAppSourceContext>()
            );
        })
        .add_extract([](World& source, World& sub) {
            sub.resource<SubValue>().value =
                static_cast<const World&>(source).resource<MainValue>().value;
        })
        .add_post_update([](World& source, World& sub) {
            source.add_resource(
                SourceOutput {.value = sub.resource<SubValue>().value}
            );
        });
    app.insert_sub_app<TestSubApp>(std::move(sub_app));
    app.set_sub_app_source<TestSubApp>([&](World& main_world) {
        return use_alternate ? SubAppSource {&alternate_world, 1} :
                               SubAppSource {&main_world, 0};
    });

    app.render();
    REQUIRE(app.world().resource<SourceOutput>().value == 7);
    REQUIRE_FALSE(alternate_world.has_resource<SourceOutput>());

    app.render();
    use_alternate = true;
    app.render();
    REQUIRE(alternate_world.resource<SourceOutput>().value == 19);

    const auto& contexts =
        app.sub_app<TestSubApp>().resource<SourceTrace>().contexts;
    REQUIRE(contexts.size() == 3);
    REQUIRE(contexts[0].changed);
    REQUIRE_FALSE(contexts[1].changed);
    REQUIRE(contexts[2].changed);
    REQUIRE(contexts[2].id == 1);
    REQUIRE(contexts[2].revision == contexts[1].revision + 1);
}

TEST_CASE(
    "Sub app source invalidation resets in-place extraction state",
    "[app][sub-app][source]"
) {
    struct SourceTrace {
        std::vector<SubAppSourceContext> contexts;
    };

    World source;
    SubApp sub_app;
    sub_app.add_resource(SourceTrace {})
        .set_pre_extract([](World&, World& sub) {
            sub.resource<SourceTrace>().contexts.push_back(
                sub.resource<SubAppSourceContext>()
            );
        });

    sub_app.extract(source);
    sub_app.extract(source);
    sub_app.invalidate_source();
    sub_app.extract(source);

    const auto& contexts = sub_app.resource<SourceTrace>().contexts;
    REQUIRE(contexts.size() == 3);
    REQUIRE(contexts[0].changed);
    REQUIRE_FALSE(contexts[1].changed);
    REQUIRE(contexts[2].changed);
    REQUIRE(contexts[2].revision == contexts[1].revision + 1);
}

TEST_CASE(
    "Sub app extraction runs pre and post hooks in order",
    "[app][sub-app]"
) {
    App app;
    app.add_resource(MainValue {});

    SubApp sub_app;
    sub_app.add_resource(SubTrace {})
        .set_pre_extract([](World&, World& sub) {
            sub.resource<SubTrace>().entries.emplace_back("pre");
        })
        .add_extract([](World&, World& sub) {
            sub.resource<SubTrace>().entries.emplace_back("extract");
        })
        .set_post_extract([](World&, World& sub) {
            sub.resource<SubTrace>().entries.emplace_back("post");
        });
    app.insert_sub_app<TestSubApp>(std::move(sub_app));

    app.render();

    REQUIRE(
        app.sub_app<TestSubApp>().resource<SubTrace>().entries ==
        std::vector<std::string> {"pre", "extract", "post"}
    );
}

TEST_CASE(
    "App delegates the complete sub app lifecycle to its runner",
    "[app][sub-app][runner]"
) {
    App app;
    SubApp sub_app;
    auto trace = std::make_shared<std::vector<std::string>>();

    app.insert_sub_app<TestSubApp>(
        std::make_unique<RecordingSubAppRunner>(std::move(sub_app), trace)
    );
    app.render();
    app.shutdown();

    REQUIRE(
        *trace == std::vector<std::string> {
                      "finish",
                      "startup",
                      "update",
                      "shutdown",
                  }
    );
}

TEST_CASE(
    "Threaded sub app runner executes schedules on its dedicated thread",
    "[app][sub-app][runner][threaded]"
) {
    const auto caller_thread = std::this_thread::get_id();
    App app;
    SubApp sub_app;
    sub_app.add_startup_schedule(SubStartup)
        .add_update_schedule(SubUpdate)
        .add_resource(ThreadObservation {})
        .add_systems(
            SubStartup,
            observe_thread_startup,
            execute_on_platform_main_thread
        )
        .add_systems(SubUpdate, observe_thread_update);
    app.insert_sub_app<TestSubApp>(
        std::make_unique<ThreadedSubAppRunner>(std::move(sub_app))
    );

    app.render();
    const auto& observation =
        app.sub_app<TestSubApp>().resource<ThreadObservation>();

    REQUIRE(
        app.sub_app_runner<TestSubApp>().execution_mode() ==
        SubAppExecutionMode::DedicatedThread
    );
    REQUIRE(observation.startup_thread != caller_thread);
    REQUIRE(observation.update_thread == observation.startup_thread);
    REQUIRE(observation.platform_main_thread == caller_thread);
}

TEST_CASE(
    "Threaded sub app runner owns bootstrap and destruction on its worker",
    "[app][sub-app][runner][threaded][lifetime]"
) {
    const auto caller_thread = std::this_thread::get_id();
    auto lifetime = std::make_shared<WorkerLifetimeState>();

    App app;
    app.insert_sub_app<TestSubApp>(
        std::make_unique<ThreadedSubAppRunner>(SubApp {})
    );
    app.sub_app_runner<TestSubApp>().run_on_execution_thread([lifetime](
                                                                 SubApp& sub_app
                                                             ) {
        sub_app.add_resource(WorkerLifetimeProbe(
            lifetime,
            static_cast<const SubApp&>(sub_app).resource<MainThreadExecutor>()
        ));
    });

    REQUIRE(lifetime->created_on != caller_thread);
    REQUIRE(lifetime->destroyed_on == std::thread::id {});

    app.shutdown();

    REQUIRE(lifetime->destroyed_on == lifetime->created_on);
    REQUIRE(lifetime->platform_cleanup_on == caller_thread);
}

TEST_CASE(
    "Threaded sub app runs shutdown callbacks in reverse order on its worker",
    "[app][sub-app][runner][threaded][shutdown]"
) {
    const auto caller_thread = std::this_thread::get_id();
    auto trace = std::make_shared<std::vector<int>>();
    auto shutdown_thread = std::make_shared<std::thread::id>();

    SubApp sub_app;
    sub_app
        .add_shutdown([trace](World&) {
            trace->push_back(1);
        })
        .add_shutdown([trace, shutdown_thread](World&) {
            trace->push_back(2);
            *shutdown_thread = std::this_thread::get_id();
        });

    App app;
    app.insert_sub_app<TestSubApp>(
        std::make_unique<ThreadedSubAppRunner>(std::move(sub_app))
    );
    app.shutdown();
    app.shutdown();

    REQUIRE(*trace == std::vector<int> {2, 1});
    REQUIRE(*shutdown_thread != caller_thread);
}

TEST_CASE(
    "Threaded sub app runner propagates bootstrap exceptions",
    "[app][sub-app][runner][threaded]"
) {
    App app;
    app.insert_sub_app<TestSubApp>(
        std::make_unique<ThreadedSubAppRunner>(SubApp {})
    );

    REQUIRE_THROWS_AS(
        app.sub_app_runner<TestSubApp>().run_on_execution_thread([](SubApp&) {
            throw std::runtime_error("threaded bootstrap failure");
        }),
        std::runtime_error
    );
    app.shutdown();
}

TEST_CASE(
    "Threaded sub app runner applies back pressure at the next frame boundary",
    "[app][sub-app][runner][threaded]"
) {
    using namespace std::chrono_literals;

    auto state = std::make_shared<BlockingUpdateState>();
    App app;
    SubApp sub_app;
    sub_app.add_update_schedule(SubUpdate).add_systems(SubUpdate, [state]() {
        std::unique_lock lock(state->mutex);
        state->started = true;
        state->changed.notify_all();
        state->changed.wait(lock, [&state]() {
            return state->released;
        });
        state->finished = true;
        state->changed.notify_all();
    });
    app.insert_sub_app<TestSubApp>(
        std::make_unique<ThreadedSubAppRunner>(std::move(sub_app))
    );

    app.render();
    {
        std::unique_lock lock(state->mutex);
        REQUIRE(state->changed.wait_for(lock, 2s, [&state]() {
            return state->started;
        }));
        REQUIRE_FALSE(state->finished);
    }

    auto next_frame = std::async(std::launch::async, [&app]() {
        app.render();
    });
    REQUIRE(next_frame.wait_for(50ms) == std::future_status::timeout);

    {
        std::scoped_lock lock(state->mutex);
        state->released = true;
    }
    state->changed.notify_all();

    REQUIRE(next_frame.wait_for(2s) == std::future_status::ready);
    next_frame.get();
}

TEST_CASE(
    "Threaded sub app runner propagates worker exceptions",
    "[app][sub-app][runner][threaded]"
) {
    App app;
    SubApp sub_app;
    sub_app.add_update_schedule(SubUpdate).add_systems(SubUpdate, []() {
        throw std::runtime_error("threaded sub app failure");
    });
    app.insert_sub_app<TestSubApp>(
        std::make_unique<ThreadedSubAppRunner>(std::move(sub_app))
    );

    app.render();
    REQUIRE_THROWS_AS(app.render(), std::runtime_error);
    app.shutdown();
}

TEST_CASE(
    "Main world services outlive sub app resources",
    "[app][sub-app][lifetime]"
) {
    auto main_alive = std::make_shared<bool>(true);
    bool sub_destroyed_while_main_alive = false;

    {
        App app;
        app.add_resource(MainLifetimeProbe(main_alive));

        SubApp sub_app;
        sub_app.add_readonly_resource_ref(app.resource<MainLifetimeProbe>())
            .add_resource(
                SubLifetimeProbe(main_alive, sub_destroyed_while_main_alive)
            );
        app.insert_sub_app<TestSubApp>(std::move(sub_app));
    }

    REQUIRE(sub_destroyed_while_main_alive);
    REQUIRE_FALSE(*main_alive);
}
