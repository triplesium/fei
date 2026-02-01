#pragma once
#include "app/app.hpp"
#include "app/plugin.hpp"
#include "asset/embed.hpp"

EMBED(shadow_vert, "shadow.vert");
EMBED(shadow_frag, "shadow.frag");
EMBED(forward_vert, "forward.vert");
EMBED(forward_frag, "forward.frag");
EMBED(color_frag, "color.frag");

namespace fei {

class PbrPlugin : public Plugin {
  public:
    void setup(App& app) override;
};

} // namespace fei
