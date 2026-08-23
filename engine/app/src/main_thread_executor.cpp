#include "app/main_thread_executor.hpp"

#include "profiling/profiling.hpp"

#include <deque>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace ets {

class MainThreadExecutor::State {
  public:
    std::thread::id owner_thread {std::this_thread::get_id()};
    std::mutex mutex;
    std::deque<MoveOnlyFunction<void()>> tasks;
    bool closed {false};
};

MainThreadExecutor::MainThreadExecutor() : m_state(std::make_shared<State>()) {}

bool MainThreadExecutor::is_main_thread() const noexcept {
    return std::this_thread::get_id() == m_state->owner_thread;
}

bool MainThreadExecutor::run_one() const {
    if (!is_main_thread()) {
        throw std::logic_error(
            "MainThreadExecutor tasks must be pumped by its owner thread"
        );
    }

    MoveOnlyFunction<void()> task;
    {
        std::scoped_lock lock(m_state->mutex);
        if (m_state->tasks.empty()) {
            return false;
        }
        task = std::move(m_state->tasks.front());
        m_state->tasks.pop_front();
    }
    {
        ETS_PROFILE_SCOPE("Main Thread Executor Task");
        task();
    }
    return true;
}

std::size_t MainThreadExecutor::run_pending() const {
    std::size_t task_count = 0;
    while (run_one()) {
        ++task_count;
    }
    return task_count;
}

bool MainThreadExecutor::has_pending() const {
    std::scoped_lock lock(m_state->mutex);
    return !m_state->tasks.empty();
}

void MainThreadExecutor::close() const noexcept {
    std::scoped_lock lock(m_state->mutex);
    m_state->closed = true;
}

void MainThreadExecutor::enqueue(MoveOnlyFunction<void()> task) const {
    std::scoped_lock lock(m_state->mutex);
    if (m_state->closed) {
        throw std::runtime_error("MainThreadExecutor is closed");
    }
    m_state->tasks.push_back(std::move(task));
}

} // namespace ets
