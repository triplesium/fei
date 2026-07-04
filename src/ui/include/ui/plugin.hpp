#pragma once
#include "app/plugin.hpp"
#include "asset/embed.hpp"

EMBED(Cousine_Regular_ttf, "Cousine-Regular.ttf");

namespace fei {

class UIPlugin : public Plugin {
  public:
    void setup(App& app) override;
};

} // namespace fei
