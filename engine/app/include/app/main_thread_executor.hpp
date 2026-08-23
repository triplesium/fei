#pragma once

#include "base/move_only_function.hpp"

#include <cstddef>
#include <functional>
#include <future>
#include <memory>
#include <type_traits>
#include <utility>

namespace ets {

// Executes platform work on the thread that created the executor. Worker calls
// block until the owner pumps the task with run_one() or run_pending().
class MainThreadExecutor {
  public:
    MainThreadExecutor();

    [[nodiscard]] bool is_main_thread() const noexcept;

    template<typename F>
    auto execute(F&& function) const -> std::invoke_result_t<std::decay_t<F>&> {
        using Function = std::decay_t<F>;
        using Result = std::invoke_result_t<Function&>;

        Function owned_function(std::forward<F>(function));
        if (is_main_thread()) {
            if constexpr (std::is_void_v<Result>) {
                std::invoke(owned_function);
                return;
            } else {
                return std::invoke(owned_function);
            }
        }

        auto task = std::make_shared<std::packaged_task<Result()>>(
            std::move(owned_function)
        );
        auto result = task->get_future();
        enqueue([task]() mutable {
            (*task)();
        });
        if constexpr (std::is_void_v<Result>) {
            result.get();
            return;
        } else {
            return result.get();
        }
    }

    // These methods may only be called by the executor's owner thread.
    bool run_one() const;
    std::size_t run_pending() const;
    [[nodiscard]] bool has_pending() const;
    void close() const noexcept;

  private:
    class State;
    std::shared_ptr<State> m_state;

    void enqueue(MoveOnlyFunction<void()> task) const;
};

} // namespace ets
