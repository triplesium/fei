#pragma once

#include "app/plugin.hpp"
#include "refl/reflect.hpp"

namespace fei {

FEI_REFLECT()
struct ImGuiInputCapture {
    bool mouse {false};
    bool keyboard {false};
    bool text {false};
};

struct ImGuiPluginConfig {
    bool docking {false};
};

class ImGuiPlugin : public Plugin {
  public:
    explicit ImGuiPlugin(ImGuiPluginConfig config = {}) : m_config(config) {}

    void setup(App& app) override;
    void cleanup(App& app) noexcept override;

  private:
    ImGuiPluginConfig m_config;
};

} // namespace fei
