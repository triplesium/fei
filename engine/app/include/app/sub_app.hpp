#pragma once

#include "ecs/system_config.hpp"
#include "ecs/system_set.hpp"
#include "ecs/world.hpp"

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

namespace fei {

using SubAppSourceId = std::uint64_t;

// Selects the World a SubApp extracts from and publishes outputs to. The World
// must remain alive until the runner has completed the submitted frame. Change
// id when reconstructing a logical source in place.
struct SubAppSource {
    World* world {nullptr};
    SubAppSourceId id {0};
};

// Stored in every SubApp World and updated immediately before extraction.
struct SubAppSourceContext {
    SubAppSourceId id {0};
    std::uint64_t revision {0};
    bool changed {false};
};

class SubApp {
  public:
    using ExtractFn = std::move_only_function<void(World&, World&)>;
    using ShutdownFn = std::move_only_function<void(World&)>;

    SubApp();
    SubApp(const SubApp&) = delete;
    SubApp& operator=(const SubApp&) = delete;
    SubApp(SubApp&&) noexcept = default;
    SubApp& operator=(SubApp&&) noexcept = default;
    ~SubApp() = default;

    SubApp& add_startup_schedule(ScheduleId schedule);
    SubApp& add_update_schedule(ScheduleId schedule);

    SubApp& set_extract_before_startup(bool enabled = true) {
        m_extract_before_startup = enabled;
        return *this;
    }

    [[nodiscard]] bool extracts_before_startup() const {
        return m_extract_before_startup;
    }

    SubApp& add_systems(
        ScheduleId schedule,
        std::convertible_to<SystemConfigs> auto&&... systems
    ) {
        m_world.add_systems(
            schedule,
            std::forward<decltype(systems)>(systems)...
        );
        return *this;
    }

    SubApp& configure_sets(
        ScheduleId schedule,
        std::convertible_to<SystemSetConfigs> auto&&... config
    ) {
        m_world.configure_sets(
            schedule,
            std::forward<decltype(config)>(config)...
        );
        return *this;
    }

    template<typename R>
    SubApp& add_resource() {
        m_world.add_resource(R {});
        return *this;
    }

    template<FromWorld R>
    SubApp& init_resource() {
        m_world.init_resource<R>();
        return *this;
    }

    template<typename R>
    SubApp& add_resource(R&& resource) {
        m_world.add_resource(std::forward<R>(resource));
        return *this;
    }

    template<typename T, typename U>
    SubApp& add_resource_as(U&& resource) {
        m_world.add_resource_as<T>(std::forward<U>(resource));
        return *this;
    }

    template<typename R>
    SubApp& add_readonly_resource_ref(R& resource) {
        m_world.add_readonly_resource_ref(resource);
        return *this;
    }

    template<typename R>
    [[nodiscard]] bool has_resource() const {
        return m_world.has_resource<R>();
    }

    template<typename R>
    R& resource() {
        return m_world.resource<R>();
    }

    template<typename R>
    const R& resource() const {
        return m_world.resource<R>();
    }

    World& world() { return m_world; }
    const World& world() const { return m_world; }

    SubApp& set_pre_extract(ExtractFn extract);
    SubApp& add_extract(ExtractFn extract);
    SubApp& set_post_extract(ExtractFn extract);
    void extract(World& source_world, SubAppSourceId source_id = 0);
    SubApp& add_post_update(ExtractFn post_update);
    SubApp& add_post_update_cleanup(ExtractFn cleanup);
    void post_update(World& main_world);
    SubApp& add_shutdown(ShutdownFn shutdown);
    void shutdown() noexcept;

    SubApp& set_worker_threads(std::size_t thread_count) {
        m_world.set_worker_threads(thread_count);
        return *this;
    }

    void finish();
    void startup();
    void update();
    void run_schedule(ScheduleId schedule) { m_world.run_schedule(schedule); }

  private:
    World m_world;
    std::vector<ScheduleId> m_startup_schedules;
    std::vector<ScheduleId> m_update_schedules;
    ExtractFn m_pre_extract;
    std::vector<ExtractFn> m_extractors;
    ExtractFn m_post_extract;
    std::vector<ExtractFn> m_post_updates;
    std::vector<ExtractFn> m_post_update_cleanups;
    std::vector<ShutdownFn> m_shutdowns;
    World* m_source_world {nullptr};
    SubAppSourceId m_source_id {0};
    std::uint64_t m_source_revision {0};
    bool m_source_initialized {false};
    bool m_extract_before_startup {false};
    bool m_finished {false};
    bool m_started {false};
    bool m_shutdown {false};
};

} // namespace fei
