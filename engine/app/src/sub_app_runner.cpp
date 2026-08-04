#include "app/sub_app_runner.hpp"

#include "app/main_thread_executor.hpp"
#include "base/log.hpp"
#include "profiling/profiling.hpp"

#include <chrono>
#include <condition_variable>
#include <exception>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <thread>
#include <utility>

namespace fei {

InlineSubAppRunner::InlineSubAppRunner(SubApp sub_app) :
    m_sub_app(std::move(sub_app)) {}

InlineSubAppRunner::~InlineSubAppRunner() {
    shutdown();
}

void InlineSubAppRunner::run_on_execution_thread(ExecutionTask task) {
    if (!task) {
        throw std::invalid_argument("SubApp execution task cannot be empty");
    }
    task(m_sub_app);
}

void InlineSubAppRunner::set_worker_threads(std::size_t thread_count) {
    m_sub_app.set_worker_threads(thread_count);
}

void InlineSubAppRunner::finish() {
    m_sub_app.finish();
}

void InlineSubAppRunner::startup(SubAppSource source) {
    if (source.world == nullptr) {
        throw std::invalid_argument("SubApp source World cannot be null");
    }
    if (m_sub_app.extracts_before_startup()) {
        m_sub_app.extract(*source.world, source.id);
    }
    m_sub_app.startup();
}

void InlineSubAppRunner::update(SubAppSource source) {
    if (source.world == nullptr) {
        throw std::invalid_argument("SubApp source World cannot be null");
    }
    m_sub_app.extract(*source.world, source.id);
    m_sub_app.update();
    m_sub_app.post_update(*source.world);
}

void InlineSubAppRunner::shutdown() noexcept {
    m_sub_app.shutdown();
}

namespace {

template<typename T>
class CapacityOneChannel {
  public:
    CapacityOneChannel() = default;
    CapacityOneChannel(const CapacityOneChannel&) = delete;
    CapacityOneChannel& operator=(const CapacityOneChannel&) = delete;

    bool send(T value) {
        std::unique_lock lock(m_mutex);
        m_changed.wait(lock, [this]() {
            return !m_value || m_closed;
        });
        if (m_closed) {
            return false;
        }
        m_value.emplace(std::move(value));
        lock.unlock();
        m_changed.notify_all();
        return true;
    }

    std::optional<T>
    receive(const MainThreadExecutor* main_thread_executor = nullptr) {
        std::unique_lock lock(m_mutex);
        while (!m_value && !m_closed) {
            if (!main_thread_executor ||
                !main_thread_executor->is_main_thread()) {
                m_changed.wait(lock, [this]() {
                    return m_value || m_closed;
                });
                continue;
            }

            lock.unlock();
            const auto ran_task = main_thread_executor->run_one();
            lock.lock();
            if (!ran_task && !m_value && !m_closed) {
                m_changed
                    .wait_for(lock, std::chrono::milliseconds(1), [this]() {
                        return m_value || m_closed;
                    });
            }
        }
        if (!m_value) {
            return std::nullopt;
        }
        auto value = std::move(m_value);
        m_value.reset();
        lock.unlock();
        m_changed.notify_all();
        return value;
    }

    void close() noexcept {
        {
            std::scoped_lock lock(m_mutex);
            m_closed = true;
        }
        m_changed.notify_all();
    }

  private:
    std::mutex m_mutex;
    std::condition_variable m_changed;
    std::optional<T> m_value;
    bool m_closed {false};
};

enum class SubAppWorkKind : std::uint8_t {
    Execute,
    Startup,
    Update,
    Shutdown,
};

struct SubAppWork {
    std::unique_ptr<SubApp> sub_app;
    SubAppWorkKind kind;
    SubAppRunner::ExecutionTask task;
};

struct SubAppWorkResult {
    std::unique_ptr<SubApp> sub_app;
    std::exception_ptr exception;
};

void log_shutdown_exception(const std::exception_ptr& exception) noexcept {
    if (!exception) {
        return;
    }
    try {
        std::rethrow_exception(exception);
    } catch (const std::exception& error_value) {
        error("SubApp runner failed during shutdown: {}", error_value.what());
    } catch (...) {
        error("SubApp runner failed during shutdown with an unknown exception");
    }
}

} // namespace

class ThreadedSubAppRunner::Impl {
  public:
    explicit Impl(SubApp sub_app) :
        m_sub_app(std::make_unique<SubApp>(std::move(sub_app))) {
        local_sub_app().add_readonly_resource_ref(m_main_thread_executor);
    }

    ~Impl() { shutdown(); }

    SubApp& sub_app() {
        collect_completed_work();
        return local_sub_app();
    }

    const SubApp& sub_app() const { return const_cast<Impl*>(this)->sub_app(); }

    void run_on_execution_thread(SubAppRunner::ExecutionTask task) {
        if (!task) {
            throw std::invalid_argument(
                "SubApp execution task cannot be empty"
            );
        }
        collect_completed_work();
        throw_if_worker_failed();
        start_worker();
        submit_work(SubAppWorkKind::Execute, std::move(task));
        collect_completed_work();
        throw_if_worker_failed();
    }

    void set_worker_threads(std::size_t thread_count) {
        collect_completed_work();
        throw_if_worker_failed();
        local_sub_app().set_worker_threads(thread_count);
    }

    void synchronize() {
        collect_completed_work();
        throw_if_worker_failed();
    }

    void finish() {
        collect_completed_work();
        throw_if_worker_failed();
        local_sub_app().finish();
    }

    void startup(SubAppSource source) {
        if (m_startup_completed) {
            return;
        }
        if (source.world == nullptr) {
            throw std::invalid_argument("SubApp source World cannot be null");
        }
        m_source_world = source.world;
        if (local_sub_app().extracts_before_startup()) {
            local_sub_app().extract(*source.world, source.id);
        }

        start_worker();
        submit_work(SubAppWorkKind::Startup);
        collect_completed_work();
        throw_if_worker_failed();
        m_startup_completed = true;
    }

    void update(SubAppSource source) {
        FEI_PROFILE_SCOPE("Threaded SubApp Update");
        if (source.world == nullptr) {
            throw std::invalid_argument("SubApp source World cannot be null");
        }
        collect_completed_work();
        throw_if_worker_failed();
        m_source_world = source.world;

        {
            FEI_PROFILE_SCOPE("Threaded SubApp Extract");
            local_sub_app().extract(*source.world, source.id);
        }
        {
            FEI_PROFILE_SCOPE("Threaded SubApp Submit");
            submit_work(SubAppWorkKind::Update);
        }
    }

    void shutdown() noexcept {
        if (m_shutdown) {
            return;
        }

        const auto failure_was_reported = m_failure_reported;
        try {
            collect_completed_work();
        } catch (...) {
            if (!failure_was_reported) {
                log_shutdown_exception(std::current_exception());
            }
        }

        bool shutdown_submitted = false;
        try {
            if (m_sub_app) {
                start_worker();
                auto work = SubAppWork {
                    .sub_app = std::move(m_sub_app),
                    .kind = SubAppWorkKind::Shutdown,
                    .task = {},
                };
                shutdown_submitted = m_to_worker.send(std::move(work));
                if (!shutdown_submitted) {
                    error("SubApp worker stopped before accepting shutdown");
                }
            }
        } catch (...) {
            log_shutdown_exception(std::current_exception());
        }
        if (shutdown_submitted) {
            auto result = m_from_worker.receive(&m_main_thread_executor);
            if (!result) {
                error("SubApp worker stopped before completing shutdown");
            } else {
                log_shutdown_exception(result->exception);
            }
        }
        m_to_worker.close();
        if (m_worker.joinable()) {
            m_worker.join();
        }
        m_from_worker.close();
        m_main_thread_executor.close();
        m_shutdown = true;
    }

  private:
    void start_worker() {
        if (m_worker.joinable()) {
            return;
        }
        m_worker = std::thread([this]() {
            worker_loop();
        });
    }

    void worker_loop() noexcept {
        while (auto work = m_to_worker.receive()) {
            if (work->kind == SubAppWorkKind::Shutdown) {
                work->sub_app->shutdown();
                work->sub_app.reset();
                static_cast<void>(m_from_worker.send(
                    SubAppWorkResult {
                        .sub_app = {},
                        .exception = {},
                    }
                ));
                break;
            }

            std::exception_ptr exception;
            try {
                FEI_PROFILE_SCOPE("Threaded SubApp Worker Execute");
                switch (work->kind) {
                    case SubAppWorkKind::Execute:
                        work->task(*work->sub_app);
                        break;
                    case SubAppWorkKind::Startup:
                        work->sub_app->startup();
                        break;
                    case SubAppWorkKind::Update:
                        work->sub_app->update();
                        break;
                    case SubAppWorkKind::Shutdown:
                        break;
                }
            } catch (...) {
                exception = std::current_exception();
            }

            if (!m_from_worker.send(
                    SubAppWorkResult {
                        .sub_app = std::move(work->sub_app),
                        .exception = exception,
                    }
                )) {
                break;
            }
        }
        m_from_worker.close();
    }

    void
    submit_work(SubAppWorkKind kind, SubAppRunner::ExecutionTask task = {}) {
        if (!m_sub_app) {
            throw std::logic_error("SubApp is already running on its worker");
        }
        if (m_worker_failure) {
            throw_if_worker_failed();
        }

        auto work = SubAppWork {
            .sub_app = std::move(m_sub_app),
            .kind = kind,
            .task = std::move(task),
        };
        m_work_in_flight = true;
        m_post_update_pending = kind == SubAppWorkKind::Update;
        if (!m_to_worker.send(std::move(work))) {
            m_work_in_flight = false;
            m_post_update_pending = false;
            throw std::runtime_error(
                "SubApp worker stopped before accepting work"
            );
        }
    }

    void collect_completed_work() {
        if (!m_work_in_flight) {
            return;
        }

        std::optional<SubAppWorkResult> result;
        {
            FEI_PROFILE_SCOPE("Threaded SubApp Worker Wait");
            result = m_from_worker.receive(&m_main_thread_executor);
        }
        m_work_in_flight = false;
        if (!result) {
            m_post_update_pending = false;
            throw std::runtime_error(
                "SubApp worker stopped without returning its app"
            );
        }

        m_sub_app = std::move(result->sub_app);
        if (!m_sub_app) {
            throw std::runtime_error("SubApp worker returned an empty app");
        }
        if (result->exception) {
            m_worker_failure = result->exception;
            m_post_update_pending = false;
            throw_if_worker_failed();
        }

        if (m_post_update_pending && m_source_world) {
            m_post_update_pending = false;
            local_sub_app().post_update(*m_source_world);
        }
    }

    SubApp& local_sub_app() {
        if (!m_sub_app) {
            throw std::logic_error("SubApp is currently owned by its worker");
        }
        return *m_sub_app;
    }

    void throw_if_worker_failed() {
        if (!m_worker_failure) {
            return;
        }
        m_failure_reported = true;
        std::rethrow_exception(m_worker_failure);
    }

    MainThreadExecutor m_main_thread_executor;
    std::unique_ptr<SubApp> m_sub_app;
    CapacityOneChannel<SubAppWork> m_to_worker;
    CapacityOneChannel<SubAppWorkResult> m_from_worker;
    std::thread m_worker;
    World* m_source_world {nullptr};
    std::exception_ptr m_worker_failure;
    bool m_work_in_flight {false};
    bool m_post_update_pending {false};
    bool m_startup_completed {false};
    bool m_failure_reported {false};
    bool m_shutdown {false};
};

ThreadedSubAppRunner::ThreadedSubAppRunner(SubApp sub_app) :
    m_impl(std::make_unique<Impl>(std::move(sub_app))) {}

ThreadedSubAppRunner::~ThreadedSubAppRunner() {
    shutdown();
}

SubApp& ThreadedSubAppRunner::sub_app() {
    return m_impl->sub_app();
}

const SubApp& ThreadedSubAppRunner::sub_app() const {
    return m_impl->sub_app();
}

void ThreadedSubAppRunner::run_on_execution_thread(ExecutionTask task) {
    m_impl->run_on_execution_thread(std::move(task));
}

void ThreadedSubAppRunner::set_worker_threads(std::size_t thread_count) {
    m_impl->set_worker_threads(thread_count);
}

void ThreadedSubAppRunner::synchronize() {
    m_impl->synchronize();
}

void ThreadedSubAppRunner::finish() {
    m_impl->finish();
}

void ThreadedSubAppRunner::startup(SubAppSource source) {
    m_impl->startup(source);
}

void ThreadedSubAppRunner::update(SubAppSource source) {
    m_impl->update(source);
}

void ThreadedSubAppRunner::shutdown() noexcept {
    if (m_impl) {
        m_impl->shutdown();
    }
}

} // namespace fei
