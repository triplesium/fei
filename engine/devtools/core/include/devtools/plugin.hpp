#pragma once

#include "app/plugin.hpp"
#include "devtools/types.hpp"

namespace fei::devtools {

FEI_REFLECT(Plugin)
class CorePlugin : public fei::Plugin {
  public:
    explicit CorePlugin(Config config = {});

    void setup(App& app) override;
    void finish(App& app) override;

  private:
    Config m_config;
};

} // namespace fei::devtools
