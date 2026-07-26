#pragma once

#include "app/plugin.hpp"
#include "ecs/system_set.hpp"

namespace fei {

struct SpriteSystems {
    struct QueueSprites : SystemSet<QueueSprites> {};
    struct RenderSprites : SystemSet<RenderSprites> {};
};

class SpritePlugin : public Plugin {
  public:
    void setup(App& app) override;
};

} // namespace fei
