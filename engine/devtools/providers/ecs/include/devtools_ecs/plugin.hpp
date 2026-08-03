#pragma once

#include "app/plugin.hpp"

namespace fei::devtools::ecs {

class ProviderPlugin : public fei::Plugin {
  public:
    void setup(App& app) override;
    void finish(App& app) override;
};

} // namespace fei::devtools::ecs
