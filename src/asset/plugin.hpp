#pragma once
#include "app/plugin.hpp"
#include "asset/server.hpp"

namespace fei {

class AssetPlugin : public Plugin {
  public:
    virtual void setup(App& app) override {
        app.add_resource(AssetServer {&app});
    }
};

} // namespace fei
