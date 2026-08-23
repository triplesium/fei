#pragma once

#include "app/plugin.hpp"
#include "base/types.hpp"

namespace ets::devtools::rendering {

struct Config {
    int jpeg_quality {80};
    uint32 max_capture_fps {15};
};

ETS_REFLECT(Plugin)
class ProviderPlugin : public ets::Plugin {
  public:
    explicit ProviderPlugin(Config config = {});

    void setup(App& app) override;
    void finish(App& app) override;

  private:
    Config m_config;
};

} // namespace ets::devtools::rendering
