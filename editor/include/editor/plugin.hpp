#pragma once

#include "app/plugin.hpp"
#include "asset/path.hpp"
#include "base/optional.hpp"
#include "ecs/fwd.hpp"

namespace fei::editor {

struct EditorPluginConfig {
    bool create_welcome_scene {true};
};

struct Selection {
    Optional<Entity> entity;
    Optional<AssetPath> asset;
};

class EditorPlugin : public Plugin {
  public:
    explicit EditorPlugin(EditorPluginConfig config = {}) : m_config(config) {}

    void setup(App& app) override;
    void cleanup(App& app) noexcept override;

  private:
    EditorPluginConfig m_config;
};

} // namespace fei::editor
