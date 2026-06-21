#pragma once

#include "base/thread_pool.hpp"

#include <cstddef>
#include <exception>
#include <functional>
#include <future>
#include <limits>
#include <memory>
#include <mutex>
#include <queue>
#include <type_traits>
#include <utility>
#include <variant>

namespace fei {

template<typename T>
class TaskResult {
  private:
    std::variant<T, std::exception_ptr> m_result;

  public:
    explicit TaskResult(T value) : m_result(std::move(value)) {}
    explicit TaskResult(std::exception_ptr exception) : m_result(exception) {}

    bool has_value() const { return std::holds_alternative<T>(m_result); }
    explicit operator bool() const { return has_value(); }

    T& value() & {
        rethrow_if_error();
        return std::get<T>(m_result);
    }

    const T& value() const& {
        rethrow_if_error();
        return std::get<T>(m_result);
    }

    T&& value() && {
        rethrow_if_error();
        return std::move(std::get<T>(m_result));
    }

    std::exception_ptr exception() const {
        if (has_value()) {
            return nullptr;
        }
        return std::get<std::exception_ptr>(m_result);
    }

    void rethrow_if_error() const {
        if (auto error = exception()) {
            std::rethrow_exception(error);
        }
    }
};

template<>
class TaskResult<void> {
  private:
    std::exception_ptr m_exception;

  public:
    TaskResult() = default;
    explicit TaskResult(std::exception_ptr exception) :
        m_exception(exception) {}

    bool has_value() const { return m_exception == nullptr; }
    explicit operator bool() const { return has_value(); }

    void value() const { rethrow_if_error(); }

    std::exception_ptr exception() const { return m_exception; }

    void rethrow_if_error() const {
        if (m_exception) {
            std::rethrow_exception(m_exception);
        }
    }
};

class TaskCompletion {
  private:
    struct Concept {
        virtual ~Concept() = default;
        virtual void call() = 0;
    };

    template<typename F>
    struct Model : Concept {
        F func;

        template<typename G>
        explicit Model(G&& func) : func(std::forward<G>(func)) {}

        void call() override { func(); }
    };

    std::unique_ptr<Concept> m_func;

  public:
    template<typename F>
    explicit TaskCompletion(F&& func) :
        m_func(
            std::make_unique<Model<std::remove_cvref_t<F>>>(
                std::forward<F>(func)
            )
        ) {}

    TaskCompletion(TaskCompletion&&) noexcept = default;
    TaskCompletion& operator=(TaskCompletion&&) noexcept = default;

    TaskCompletion(const TaskCompletion&) = delete;
    TaskCompletion& operator=(const TaskCompletion&) = delete;

    void operator()() { m_func->call(); }
};

class TaskPool {
  private:
    std::mutex m_completion_mutex;
    std::queue<TaskCompletion> m_completions;
    ThreadPool m_thread_pool;

  public:
    explicit TaskPool(
        std::size_t thread_count = ThreadPool::default_thread_count()
    );
    ~TaskPool() = default;

    TaskPool(const TaskPool&) = delete;
    TaskPool& operator=(const TaskPool&) = delete;
    TaskPool(TaskPool&&) = delete;
    TaskPool& operator=(TaskPool&&) = delete;

    template<typename F>
    auto submit(F&& func) -> std::future<std::invoke_result_t<F>> {
        return m_thread_pool.submit(std::forward<F>(func));
    }

    template<typename Work, typename Complete>
    void submit(Work&& work, Complete&& complete) {
        using Result = std::invoke_result_t<Work&>;
        m_thread_pool.submit([this,
                              work = std::forward<Work>(work),
                              complete =
                                  std::forward<Complete>(complete)]() mutable {
            if constexpr (std::is_void_v<Result>) {
                try {
                    std::invoke(work);
                    enqueue_completion(TaskCompletion(
                        [complete = std::move(complete)]() mutable {
                            std::invoke(complete, TaskResult<void> {});
                        }
                    ));
                } catch (...) {
                    enqueue_completion(TaskCompletion(
                        [complete = std::move(complete),
                         exception = std::current_exception()]() mutable {
                            std::invoke(complete, TaskResult<void>(exception));
                        }
                    ));
                }
            } else {
                try {
                    auto value = std::invoke(work);
                    enqueue_completion(TaskCompletion(
                        [complete = std::move(complete),
                         result =
                             TaskResult<Result>(std::move(value))]() mutable {
                            std::invoke(complete, std::move(result));
                        }
                    ));
                } catch (...) {
                    enqueue_completion(
                        TaskCompletion([complete = std::move(complete),
                                        result = TaskResult<Result>(
                                            std::current_exception()
                                        )]() mutable {
                            std::invoke(complete, std::move(result));
                        })
                    );
                }
            }
        });
    }

    std::size_t thread_count() const { return m_thread_pool.thread_count(); }

    std::size_t drain_completions(
        std::size_t max_count = std::numeric_limits<std::size_t>::max()
    );

  private:
    void enqueue_completion(TaskCompletion completion);
};

} // namespace fei
