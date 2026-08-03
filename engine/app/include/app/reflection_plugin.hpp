#pragma once

#include "app/plugin.hpp"

namespace fei {

class App;

class ReflectionPlugin : public Plugin {
  public:
    void setup(App& app) override;
};

} // namespace fei
