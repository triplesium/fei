#pragma once
#include "app/app.hpp"
#include "app/plugin.hpp"

namespace fei {

class RenderingPlugin : public Plugin {
  public:
    void setup(App& app) override;
};

} // namespace fei
