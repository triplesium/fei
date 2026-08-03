#pragma once
#include "app/plugin.hpp"

namespace fei {

class ScenePlugin : public Plugin {
  public:
    void setup(App& app) override;
};

} // namespace fei
