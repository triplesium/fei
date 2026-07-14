#pragma once

#include "app/plugin.hpp"
#include "refl/reflect.hpp"

namespace fei {

struct FEI_REFLECT ImGuiInputCapture {
    bool mouse {false};
    bool keyboard {false};
    bool text {false};
};

class ImGuiPlugin : public Plugin {
  public:
    void setup(App& app) override;
    void cleanup(App& app) noexcept override;
};

} // namespace fei
