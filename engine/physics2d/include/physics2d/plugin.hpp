#pragma once

#include "app/plugin.hpp"
#include "ecs/system_set.hpp"

namespace fei {

class App;

struct PhysicsSystems2d {
    struct Sync : SystemSet<Sync> {};
    struct Step : SystemSet<Step> {};
    struct WriteBack : SystemSet<WriteBack> {};
    struct Interpolate : SystemSet<Interpolate> {};
};

FEI_REFLECT(Plugin)
class PhysicsPlugin2d : public Plugin {
  public:
    void dependencies(PluginDependencies& dependencies) const override;
    void setup(App& app) override;
};

} // namespace fei
