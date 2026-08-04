#include "app/sub_app.hpp"

#include "base/log.hpp"
#include "ecs/commands.hpp"

#include <algorithm>
#include <exception>

namespace fei {

namespace {

void append_unique(std::vector<ScheduleId>& schedules, ScheduleId schedule) {
    if (std::ranges::find(schedules, schedule) == schedules.end()) {
        schedules.push_back(schedule);
    }
}

} // namespace

SubApp::SubApp() {
    m_world.add_resource(CommandsQueue {});
}

SubApp& SubApp::add_startup_schedule(ScheduleId schedule) {
    append_unique(m_startup_schedules, schedule);
    return *this;
}

SubApp& SubApp::add_update_schedule(ScheduleId schedule) {
    append_unique(m_update_schedules, schedule);
    return *this;
}

SubApp& SubApp::set_pre_extract(ExtractFn extract) {
    m_pre_extract = std::move(extract);
    return *this;
}

SubApp& SubApp::add_extract(ExtractFn extract) {
    if (!extract) {
        fatal("Cannot add an empty SubApp extract function");
    }
    m_extractors.push_back(std::move(extract));
    return *this;
}

SubApp& SubApp::set_post_extract(ExtractFn extract) {
    m_post_extract = std::move(extract);
    return *this;
}

void SubApp::extract(World& main_world) {
    if (m_pre_extract) {
        m_pre_extract(main_world, m_world);
    }
    for (auto& extract : m_extractors) {
        extract(main_world, m_world);
    }
    if (m_post_extract) {
        m_post_extract(main_world, m_world);
    }
}

SubApp& SubApp::add_post_update(ExtractFn post_update) {
    if (!post_update) {
        fatal("Cannot add an empty SubApp post-update function");
    }
    m_post_updates.push_back(std::move(post_update));
    return *this;
}

SubApp& SubApp::add_post_update_cleanup(ExtractFn cleanup) {
    if (!cleanup) {
        fatal("Cannot add an empty SubApp post-update cleanup function");
    }
    m_post_update_cleanups.push_back(std::move(cleanup));
    return *this;
}

void SubApp::post_update(World& main_world) {
    for (auto& post_update : m_post_updates) {
        post_update(main_world, m_world);
    }
    for (auto& cleanup : m_post_update_cleanups) {
        cleanup(main_world, m_world);
    }
}

SubApp& SubApp::add_shutdown(ShutdownFn shutdown) {
    if (!shutdown) {
        fatal("Cannot add an empty SubApp shutdown function");
    }
    if (m_shutdown) {
        fatal("Cannot add a SubApp shutdown function after shutdown");
    }
    m_shutdowns.push_back(std::move(shutdown));
    return *this;
}

void SubApp::shutdown() noexcept {
    if (m_shutdown) {
        return;
    }
    m_shutdown = true;
    for (auto shutdown = m_shutdowns.rbegin(); shutdown != m_shutdowns.rend();
         ++shutdown) {
        try {
            (*shutdown)(m_world);
        } catch (const std::exception& exception) {
            error("SubApp shutdown failed: {}", exception.what());
        } catch (...) {
            error("SubApp shutdown failed with an unknown exception");
        }
    }
}

void SubApp::finish() {
    if (m_finished) {
        return;
    }
    m_world.sort_systems();
    m_finished = true;
}

void SubApp::startup() {
    if (m_started) {
        return;
    }
    finish();
    m_world.apply_deferred();
    for (const auto schedule : m_startup_schedules) {
        m_world.run_schedule(schedule);
    }
    m_started = true;
}

void SubApp::update() {
    startup();
    m_world.apply_deferred();
    for (const auto schedule : m_update_schedules) {
        m_world.run_schedule(schedule);
    }
}

} // namespace fei
