#pragma once
#include "app/app.hpp"
#include "app/plugin.hpp"

namespace fei {

class ScriptingPlugin : public Plugin {
  public:
    virtual void setup(App& app);
};

} // namespace fei
