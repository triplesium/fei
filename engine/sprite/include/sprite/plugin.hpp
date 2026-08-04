#pragma once

#include "app/plugin.hpp"
#include "ecs/system_set.hpp"
#include "sprite/output.hpp"

namespace fei {

struct SpriteSystems {
    struct PrepareOutput : SystemSet<PrepareOutput> {};
    struct QueueSprites : SystemSet<QueueSprites> {};
    struct RenderSprites : SystemSet<RenderSprites> {};
};

struct SpritePluginConfig {
    SpriteOutputMode output {SpriteOutputMode::MainSwapchain};
    uint32 width {1};
    uint32 height {1};
    PixelFormat texture_format {PixelFormat::Rgba8Unorm};
};

class SpritePlugin : public Plugin {
  public:
    explicit SpritePlugin(SpritePluginConfig config = {}) : m_config(config) {}

    void setup(App& app) override;

  private:
    SpritePluginConfig m_config;
};

} // namespace fei
