#pragma once

#include "app/plugin.hpp"

#include <cstddef>
#include <cstdint>

namespace fei::devtools::scripting_lua {

struct Config {
    std::size_t max_source_bytes {std::size_t {64} * 1024};
    std::size_t max_output_bytes {std::size_t {64} * 1024};
    std::uint64_t instruction_limit {1'000'000};
    std::uint32_t time_limit_ms {100};
};

class ProviderPlugin : public fei::Plugin {
  public:
    explicit ProviderPlugin(Config config = {});

    void setup(App& app) override;
    void finish(App& app) override;

  private:
    Config m_config;
};

} // namespace fei::devtools::scripting_lua
