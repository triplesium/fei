#pragma once
#include "app/app.hpp"

namespace fei {

class ForwardRenderPlugin : public Plugin {
  public:
    void setup(App& app) override;
};

} // namespace fei
