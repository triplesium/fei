#pragma once

#include "app/sub_app.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>

namespace fei {

enum class SubAppExecutionMode : std::uint8_t {
    Inline,
    DedicatedThread,
};

// Owns a SubApp and defines where and when its lifecycle is executed. Access
// through sub_app() is synchronized by the runner implementation.
class SubAppRunner {
  public:
    using ExecutionTask = std::move_only_function<void(SubApp&)>;

    SubAppRunner() = default;
    SubAppRunner(const SubAppRunner&) = delete;
    SubAppRunner& operator=(const SubAppRunner&) = delete;
    SubAppRunner(SubAppRunner&&) = delete;
    SubAppRunner& operator=(SubAppRunner&&) = delete;
    virtual ~SubAppRunner() = default;

    [[nodiscard]] virtual SubAppExecutionMode
    execution_mode() const noexcept = 0;
    virtual SubApp& sub_app() = 0;
    virtual const SubApp& sub_app() const = 0;

    // Runs synchronous owner-thread initialization before the SubApp starts.
    // Dedicated runners use this for thread-affine runtime bootstrap.
    virtual void run_on_execution_thread(ExecutionTask task) = 0;
    virtual void synchronize() = 0;
    virtual void set_worker_threads(std::size_t thread_count) = 0;
    virtual void finish() = 0;
    virtual void startup(SubAppSource source) = 0;
    virtual void update(SubAppSource source) = 0;
    virtual void shutdown() noexcept = 0;
};

// Preserves the original serial SubApp behavior. This is also the fallback for
// platforms and graphics backends that cannot run a dedicated render thread.
class InlineSubAppRunner final : public SubAppRunner {
  public:
    explicit InlineSubAppRunner(SubApp sub_app);
    ~InlineSubAppRunner() override;

    [[nodiscard]] SubAppExecutionMode execution_mode() const noexcept override {
        return SubAppExecutionMode::Inline;
    }

    SubApp& sub_app() override { return m_sub_app; }
    const SubApp& sub_app() const override { return m_sub_app; }

    void run_on_execution_thread(ExecutionTask task) override;
    void synchronize() override {}
    void set_worker_threads(std::size_t thread_count) override;
    void finish() override;
    void startup(SubAppSource source) override;
    void update(SubAppSource source) override;
    void shutdown() noexcept override;

  private:
    SubApp m_sub_app;
};

// Moves the entire SubApp across a pair of capacity-one channels. A submitted
// frame runs on the dedicated thread while the Main World advances. The next
// update waits for that frame, publishes its post-update outputs, extracts the
// new frame, and transfers exclusive ownership back to the worker.
class ThreadedSubAppRunner final : public SubAppRunner {
  public:
    explicit ThreadedSubAppRunner(SubApp sub_app);
    ~ThreadedSubAppRunner() override;

    [[nodiscard]] SubAppExecutionMode execution_mode() const noexcept override {
        return SubAppExecutionMode::DedicatedThread;
    }

    SubApp& sub_app() override;
    const SubApp& sub_app() const override;

    void run_on_execution_thread(ExecutionTask task) override;
    void synchronize() override;
    void set_worker_threads(std::size_t thread_count) override;
    void finish() override;
    void startup(SubAppSource source) override;
    void update(SubAppSource source) override;
    void shutdown() noexcept override;

  private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace fei
