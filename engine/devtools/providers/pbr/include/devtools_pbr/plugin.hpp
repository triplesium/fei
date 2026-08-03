#pragma once

#include "app/plugin.hpp"
#include "base/types.hpp"

namespace fei::devtools::pbr {

struct Config {
    int jpeg_quality {80};
    uint32 max_capture_fps {15};
};

class ProviderPlugin : public fei::Plugin {
  public:
    explicit ProviderPlugin(Config config = {});

    void setup(App& app) override;
    void finish(App& app) override;

  private:
    Config m_config;
};

} // namespace fei::devtools::pbr
