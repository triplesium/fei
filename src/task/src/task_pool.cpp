#include "task/task_pool.hpp"

#include <optional>

namespace fei {

TaskPool::TaskPool(std::size_t thread_count) : m_thread_pool(thread_count) {}

std::size_t TaskPool::drain_completions(std::size_t max_count) {
    std::size_t count = 0;
    while (count < max_count) {
        std::optional<TaskCompletion> completion;
        {
            std::scoped_lock lock(m_completion_mutex);
            if (m_completions.empty()) {
                break;
            }
            completion.emplace(std::move(m_completions.front()));
            m_completions.pop();
        }

        (*completion)();
        ++count;
    }
    return count;
}

void TaskPool::enqueue_completion(TaskCompletion completion) {
    std::scoped_lock lock(m_completion_mutex);
    m_completions.push(std::move(completion));
}

} // namespace fei
