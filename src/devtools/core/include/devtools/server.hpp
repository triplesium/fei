#pragma once

#include "devtools/bridge.hpp"
#include "devtools/types.hpp"

#include <memory>
#include <thread>

namespace httplib {
class Server;
}

namespace fei::devtools {

class Server {
  public:
    Server(Config config, Bridge bridge);
    ~Server();

    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;
    Server(Server&& other) noexcept;
    Server& operator=(Server&& other) noexcept;

    void start();
    void stop();

    const Config& config() const { return m_config; }

  private:
    Config m_config;
    Bridge m_bridge;
    std::unique_ptr<httplib::Server> m_server;
    std::thread m_thread;
};

} // namespace fei::devtools
