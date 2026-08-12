#pragma once

#include "base/optional.hpp"
#include "base/result.hpp"
#include "base/types.hpp"

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace fei::agentd {

struct ProcessLaunch {
    std::vector<std::string> arguments;
    std::vector<std::pair<std::string, std::string>> environment;
};

class RuntimeProcess {
  public:
    RuntimeProcess();
    RuntimeProcess(const RuntimeProcess&) = delete;
    RuntimeProcess& operator=(const RuntimeProcess&) = delete;
    RuntimeProcess(RuntimeProcess&&) = delete;
    RuntimeProcess& operator=(RuntimeProcess&&) = delete;
    ~RuntimeProcess();

    Status<std::string> start(const ProcessLaunch& launch);
    [[nodiscard]] Optional<int> poll();
    void terminate() noexcept;

    [[nodiscard]] bool running() const;
    [[nodiscard]] uint64 process_id() const;

  private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace fei::agentd
