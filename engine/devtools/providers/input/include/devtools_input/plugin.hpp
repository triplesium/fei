#pragma once

#include "app/plugin.hpp"

namespace ets::devtools::input {

ETS_REFLECT(Plugin)
class ProviderPlugin : public ets::Plugin {
  public:
    void setup(App& app) override;
    void finish(App& app) override;
};

} // namespace ets::devtools::input
