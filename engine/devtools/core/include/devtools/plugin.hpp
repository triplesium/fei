#pragma once

#include "app/plugin.hpp"
#include "devtools/types.hpp"

namespace ets::devtools {

ETS_REFLECT(Plugin)
class CorePlugin : public ets::Plugin {
  public:
    explicit CorePlugin(Config config = {});

    void setup(App& app) override;
    void finish(App& app) override;

  private:
    Config m_config;
};

} // namespace ets::devtools
