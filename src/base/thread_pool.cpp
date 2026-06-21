#include "base/thread_pool.hpp"

#include <algorithm>

namespace fei {

ThreadPool::ThreadPool(std::size_t thread_count) {
    thread_count = std::max<std::size_t>(1, thread_count);
    m_workers.reserve(thread_count);
    for (std::size_t i = 0; i < thread_count; ++i) {
        m_workers.emplace_back([this]() {
            worker_loop();
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

std::size_t ThreadPool::default_thread_count() {
    auto count = std::thread::hardware_concurrency();
    if (count == 0) {
        return 1;
    }
    return count;
}

void ThreadPool::worker_loop() {
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

} // namespace fei
