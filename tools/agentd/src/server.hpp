#pragma once

#include "base/result.hpp"
#include "base/types.hpp"

#include <memory>
#include <string>

namespace ets::agentd {

class SupervisorState;

class AgentServer {
  public:
    AgentServer(SupervisorState& state, uint16 port);
    AgentServer(const AgentServer&) = delete;
    AgentServer& operator=(const AgentServer&) = delete;
    AgentServer(AgentServer&&) = delete;
    AgentServer& operator=(AgentServer&&) = delete;
    ~AgentServer();

    Status<std::string> start();
    void stop() noexcept;

  private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace ets::agentd
