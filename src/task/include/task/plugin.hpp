#pragma once

#include "app/plugin.hpp"
#include "ecs/system_set.hpp"
#include "task/task_pool.hpp"

#include <cstddef>
#include <memory>

namespace fei {

template<typename T>
class ResRW;

struct TaskSystems {
    struct DrainCompletions : SystemSet<DrainCompletions> {};
};

class Tasks {
  private:
    std::unique_ptr<TaskPool> m_general;

  public:
    explicit Tasks(
        std::size_t thread_count = ThreadPool::default_thread_count()
    );
    ~Tasks() = default;

    Tasks(const Tasks&) = delete;
    Tasks& operator=(const Tasks&) = delete;
    Tasks(Tasks&&) noexcept = default;
    Tasks& operator=(Tasks&&) noexcept = default;

    TaskPool& general() { return *m_general; }
    const TaskPool& general() const { return *m_general; }

    std::size_t drain_completions();

    static void drain_completion_system(ResRW<Tasks> tasks);
};

class TaskPlugin : public Plugin {
  public:
    void setup(App& app) override;
};

} // namespace fei
