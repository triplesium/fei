#pragma once

#include "app/app.hpp"
#include "project/project.hpp"

namespace fei::runtime_host {

class RuntimeHostApplication {
  public:
    explicit RuntimeHostApplication(Project project);
    RuntimeHostApplication(const RuntimeHostApplication&) = delete;
    RuntimeHostApplication& operator=(const RuntimeHostApplication&) = delete;
    RuntimeHostApplication(RuntimeHostApplication&&) = delete;
    RuntimeHostApplication& operator=(RuntimeHostApplication&&) = delete;
    ~RuntimeHostApplication();

    void run();

  private:
    void shutdown() noexcept;
    void update_frame();

    App m_app;
};

} // namespace fei::runtime_host
