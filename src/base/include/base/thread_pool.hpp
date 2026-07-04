#pragma once

#include <condition_variable>
#include <cstddef>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace fei {

class ThreadPool {
  private:
    std::vector<std::thread> m_workers;
    std::queue<std::function<void()>> m_tasks;
    std::mutex m_mutex;
    std::condition_variable m_task_available;
    bool m_stopping {false};

  public:
    explicit ThreadPool(std::size_t thread_count = default_thread_count());
    ~ThreadPool();

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
    ThreadPool(ThreadPool&&) = delete;
    ThreadPool& operator=(ThreadPool&&) = delete;

    template<typename F>
    auto submit(F&& func) -> std::future<std::invoke_result_t<F>> {
        using Result = std::invoke_result_t<F>;
        auto task = std::make_shared<std::packaged_task<Result()>>(
            std::forward<F>(func)
        );
        auto future = task->get_future();

        {
            std::scoped_lock lock(m_mutex);
            m_tasks.emplace([task]() {
                (*task)();
            });
        }
        m_task_available.notify_one();

        return future;
    }

    std::size_t thread_count() const { return m_workers.size(); }

    static std::size_t default_thread_count();

  private:
    void worker_loop();
};

} // namespace fei
