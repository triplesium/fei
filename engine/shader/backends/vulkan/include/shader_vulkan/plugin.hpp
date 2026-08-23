#pragma once
#include "app/plugin.hpp"

namespace ets {

ETS_REFLECT(Plugin)
class VulkanShaderPlugin final : public Plugin {
  public:
    void setup(App& app) override;
};

} // namespace ets
