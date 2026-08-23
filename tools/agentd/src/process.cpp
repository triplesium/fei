#include "process.hpp"

#include <string>
#include <utility>
#include <vector>

#if defined(_WIN32)
#    include <Windows.h> // IWYU pragma: keep
#else
#    include <csignal>
#    include <cstdlib>
#    include <sys/types.h>
#    include <sys/wait.h>
#    include <unistd.h>
#endif

namespace ets::agentd {
namespace {

#if defined(_WIN32)

Result<std::wstring, std::string> to_wide(std::string_view value) {
    if (value.empty()) {
        return std::wstring {};
    }
    const auto required = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        value.data(),
        static_cast<int>(value.size()),
        nullptr,
        0
    );
    if (required <= 0) {
        return failure(std::string("Failed to decode UTF-8 process argument"));
    }
    std::wstring result(static_cast<std::size_t>(required), L'\0');
    if (MultiByteToWideChar(
            CP_UTF8,
            MB_ERR_INVALID_CHARS,
            value.data(),
            static_cast<int>(value.size()),
            result.data(),
            required
        ) != required) {
        return failure(std::string("Failed to decode UTF-8 process argument"));
    }
    return result;
}

std::wstring quote_windows_argument(std::wstring_view argument) {
    if (argument.find_first_of(L" \t\"") == std::wstring_view::npos) {
        return std::wstring(argument);
    }

    std::wstring result {L'\"'};
    std::size_t backslashes = 0;
    for (const auto character : argument) {
        if (character == L'\\') {
            ++backslashes;
            continue;
        }
        if (character == L'\"') {
            result.append(backslashes * 2 + 1, L'\\');
            result.push_back(L'\"');
            backslashes = 0;
            continue;
        }
        result.append(backslashes, L'\\');
        backslashes = 0;
        result.push_back(character);
    }
    result.append(backslashes * 2, L'\\');
    result.push_back(L'\"');
    return result;
}

Result<std::wstring, std::string>
make_windows_command_line(const std::vector<std::string>& arguments) {
    std::wstring command_line;
    for (const auto& argument : arguments) {
        auto wide = to_wide(argument);
        if (!wide) {
            return failure(std::move(wide.error()));
        }
        if (!command_line.empty()) {
            command_line.push_back(L' ');
        }
        command_line += quote_windows_argument(*wide);
    }
    return command_line;
}

class ScopedEnvironment {
  public:
    explicit ScopedEnvironment(
        const std::vector<std::pair<std::string, std::string>>& values
    ) {
        for (const auto& [name, value] : values) {
            auto wide_name = to_wide(name);
            auto wide_value = to_wide(value);
            if (!wide_name || !wide_value) {
                continue;
            }
            const auto required =
                GetEnvironmentVariableW(wide_name->c_str(), nullptr, 0);
            std::wstring previous;
            bool existed = required > 0;
            if (existed) {
                previous.resize(required);
                GetEnvironmentVariableW(
                    wide_name->c_str(),
                    previous.data(),
                    required
                );
                previous.resize(required - 1);
            }
            m_previous.emplace_back(
                std::move(*wide_name),
                std::move(previous),
                existed
            );
            SetEnvironmentVariableW(
                m_previous.back().name.c_str(),
                wide_value->c_str()
            );
        }
    }

    ~ScopedEnvironment() {
        for (auto entry = m_previous.rbegin(); entry != m_previous.rend();
             ++entry) {
            SetEnvironmentVariableW(
                entry->name.c_str(),
                entry->existed ? entry->value.c_str() : nullptr
            );
        }
    }

  private:
    struct PreviousValue {
        std::wstring name;
        std::wstring value;
        bool existed;
    };

    std::vector<PreviousValue> m_previous;
};

#endif

} // namespace

class RuntimeProcess::Impl {
  public:
    ~Impl() { terminate(); }

    Status<std::string> start(const ProcessLaunch& launch) {
        if (m_running) {
            return failure(std::string("Runtime process is already running"));
        }
        if (launch.arguments.empty() || launch.arguments.front().empty()) {
            return failure(std::string("Runtime command must not be empty"));
        }

#if defined(_WIN32)
        auto command_line = make_windows_command_line(launch.arguments);
        if (!command_line) {
            return failure(std::move(command_line.error()));
        }
        command_line->push_back(L'\0');

        STARTUPINFOW startup {};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION process {};
        ScopedEnvironment environment(launch.environment);
        if (!CreateProcessW(
                nullptr,
                command_line->data(),
                nullptr,
                nullptr,
                TRUE,
                0,
                nullptr,
                nullptr,
                &startup,
                &process
            )) {
            return failure(
                std::string("Failed to start runtime process: Windows error ") +
                std::to_string(GetLastError())
            );
        }
        CloseHandle(process.hThread);
        m_process = process.hProcess;
        m_process_id = process.dwProcessId;
#else
        const auto child = fork();
        if (child < 0) {
            return failure(std::string("Failed to fork runtime process"));
        }
        if (child == 0) {
            for (const auto& [name, value] : launch.environment) {
                setenv(name.c_str(), value.c_str(), 1);
            }
            std::vector<char*> arguments;
            arguments.reserve(launch.arguments.size() + 1);
            for (const auto& argument : launch.arguments) {
                arguments.push_back(const_cast<char*>(argument.c_str()));
            }
            arguments.push_back(nullptr);
            execvp(arguments.front(), arguments.data());
            _exit(127);
        }
        m_process_id = static_cast<uint64>(child);
#endif
        m_running = true;
        return {};
    }

    Optional<int> poll() {
        if (!m_running) {
            return nullopt;
        }
#if defined(_WIN32)
        DWORD exit_code = STILL_ACTIVE;
        if (!GetExitCodeProcess(m_process, &exit_code) ||
            exit_code == STILL_ACTIVE) {
            return nullopt;
        }
        CloseHandle(m_process);
        m_process = nullptr;
        m_running = false;
        return static_cast<int>(exit_code);
#else
        int status = 0;
        const auto result =
            waitpid(static_cast<pid_t>(m_process_id), &status, WNOHANG);
        if (result <= 0) {
            return nullopt;
        }
        m_running = false;
        if (WIFEXITED(status)) {
            return WEXITSTATUS(status);
        }
        if (WIFSIGNALED(status)) {
            return 128 + WTERMSIG(status);
        }
        return -1;
#endif
    }

    void terminate() noexcept {
        if (!m_running) {
            return;
        }
#if defined(_WIN32)
        TerminateProcess(m_process, 1);
        WaitForSingleObject(m_process, 5000);
        CloseHandle(m_process);
        m_process = nullptr;
#else
        kill(static_cast<pid_t>(m_process_id), SIGTERM);
        int status = 0;
        waitpid(static_cast<pid_t>(m_process_id), &status, 0);
#endif
        m_running = false;
    }

    [[nodiscard]] bool running() const { return m_running; }

    [[nodiscard]] uint64 process_id() const { return m_process_id; }

  private:
#if defined(_WIN32)
    HANDLE m_process {nullptr};
#endif
    uint64 m_process_id {0};
    bool m_running {false};
};

RuntimeProcess::RuntimeProcess() : m_impl(std::make_unique<Impl>()) {}

RuntimeProcess::~RuntimeProcess() = default;

Status<std::string> RuntimeProcess::start(const ProcessLaunch& launch) {
    return m_impl->start(launch);
}

Optional<int> RuntimeProcess::poll() {
    return m_impl->poll();
}

void RuntimeProcess::terminate() noexcept {
    m_impl->terminate();
}

bool RuntimeProcess::running() const {
    return m_impl->running();
}

uint64 RuntimeProcess::process_id() const {
    return m_impl->process_id();
}

} // namespace ets::agentd
