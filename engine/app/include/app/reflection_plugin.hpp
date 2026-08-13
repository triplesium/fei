#pragma once

#include "app/app.hpp"
#include "refl/reflect.hpp"

namespace fei {

FEI_REFLECT(Plugin)
class ReflectionPlugin : public Plugin {
  public:
    void setup(App& app) override;
};

} // namespace fei
