#pragma once

#include "app/plugin.hpp"

namespace fei {

class GltfPlugin : public Plugin {
  public:
    void setup(App& app) override;
};

} // namespace fei
