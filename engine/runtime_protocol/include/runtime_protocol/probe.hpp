#pragma once

#include "app/plugin.hpp"
#include "base/result.hpp"
#include "base/types.hpp"
#include "runtime_protocol/protocol.hpp"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace fei {

class World;

namespace runtime_protocol {

struct RuntimeInspectionError {
    std::string kind {"internal"};
    std::string message;
};

using RuntimeInspectionHandler =
    std::function<Result<std::string, RuntimeInspectionError>(
        World&,
        const InspectionRequest&
    )>;

struct RuntimeProbeConfig {
    std::string project;
    std::string project_file;
    std::string build_id;
    std::vector<InspectionCapability> inspections;
    uint32 heartbeat_interval_ms {500};
    RuntimeInspectionHandler inspection_handler;
};

struct RuntimeProbeStatus {
    bool enabled {false};
    bool connected {false};
    uint64 frame {0};
    std::string last_error;
};

class RuntimeProbe {
  public:
    explicit RuntimeProbe(RuntimeProbeConfig config = {});
    RuntimeProbe(const RuntimeProbe&) = delete;
    RuntimeProbe& operator=(const RuntimeProbe&) = delete;
    RuntimeProbe(RuntimeProbe&&) noexcept;
    RuntimeProbe& operator=(RuntimeProbe&&) noexcept;
    ~RuntimeProbe();

    void on_frame(World& world);
    void stop() noexcept;

    [[nodiscard]] RuntimeProbeStatus status() const;

  private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

FEI_REFLECT(Plugin)
class RuntimeProbePlugin : public Plugin {
  public:
    explicit RuntimeProbePlugin(RuntimeProbeConfig config = {}) :
        m_config(std::move(config)) {}

    void setup(App& app) override;
    void cleanup(App& app) noexcept override;

  private:
    RuntimeProbeConfig m_config;
};

} // namespace runtime_protocol

} // namespace fei
