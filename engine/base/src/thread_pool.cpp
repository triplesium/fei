#include "base/thread_pool.hpp"

namespace ets {
namespace {

thread_local const ThreadPool* c_current_thread_pool {nullptr};
thread_local std::size_t c_current_worker_index {0};

class WorkerContextScope {
  public:
    WorkerContextScope(const ThreadPool& pool, std::size_t worker_index) {
        c_current_thread_pool = &pool;
        c_current_worker_index = worker_index;
    }

    ~WorkerContextScope() { c_current_thread_pool = nullptr; }

    WorkerContextScope(const WorkerContextScope&) = delete;
    WorkerContextScope& operator=(const WorkerContextScope&) = delete;
};

} // namespace

ThreadPool::ThreadPool(std::size_t thread_count) {
    m_workers.reserve(thread_count);
    for (std::size_t i = 0; i < thread_count; ++i) {
        m_workers.emplace_back([this, i]() {
            worker_loop(i);
        });
    }
}

ThreadPool::~ThreadPool() {
    {
        std::scoped_lock lock(m_mutex);
        m_stopping = true;
    }
    m_task_available.notify_all();
    for (auto& worker : m_workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }
}

Optional<std::size_t> ThreadPool::current_worker_index() const noexcept {
    if (c_current_thread_pool != this) {
        return nullopt;
    }
    return c_current_worker_index;
}

std::size_t ThreadPool::default_thread_count() {
#ifdef __EMSCRIPTEN__
    return 0;
#else
    auto count = std::thread::hardware_concurrency();
    if (count == 0) {
        return 1;
    }
    return count;
#endif
}

void ThreadPool::worker_loop(std::size_t worker_index) {
    const WorkerContextScope context {*this, worker_index};
    while (true) {
        std::function<void()> task;
        {
            std::unique_lock lock(m_mutex);
            m_task_available.wait(lock, [this]() {
                return m_stopping || !m_tasks.empty();
            });
            if (m_stopping && m_tasks.empty()) {
                return;
            }
            task = std::move(m_tasks.front());
            m_tasks.pop();
        }
        task();
    }
}

} // namespace ets
