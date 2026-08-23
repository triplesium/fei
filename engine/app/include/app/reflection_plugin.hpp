#pragma once

#include "app/app.hpp"
#include "refl/reflect.hpp"

namespace ets {

ETS_REFLECT(Plugin)
class ReflectionPlugin : public Plugin {
  public:
    void setup(App& app) override;
};

} // namespace ets
