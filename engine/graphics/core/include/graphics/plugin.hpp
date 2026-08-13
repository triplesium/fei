#pragma once
#include "app/plugin.hpp"

namespace fei {

FEI_REFLECT(Plugin)
class GraphicsPlugin : public Plugin {
  public:
    void setup(App& app) override {}
};

} // namespace fei
