#pragma once

#include "app/plugin.hpp"
#include "base/optional.hpp"
#include "ecs/fwd.hpp"

namespace fei::editor {

struct EditorPluginConfig {
    bool create_welcome_scene {true};
};

struct Selection {
    Optional<Entity> entity;
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
