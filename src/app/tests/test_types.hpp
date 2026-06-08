#pragma once

#include "app/app.hpp"
#include "app/plugin.hpp"

namespace fei::app_test {

struct AppTestEvent {
    int value {0};
};

struct AppTestResource {
    int value {0};
};

struct AppTestResourceAlias {
    int stored_value {0};

    AppTestResourceAlias() = default;
    AppTestResourceAlias(struct AppTestResourceImpl resource);
};

struct AppTestResourceImpl {
    int stored_value {0};

    explicit AppTestResourceImpl(int value) : stored_value(value) {}
};

inline AppTestResourceAlias::AppTestResourceAlias(AppTestResourceImpl resource
) : stored_value(resource.stored_value) {}

struct FromWorldResource {
    bool had_app_states {false};

    explicit FromWorldResource(World& world) :
        had_app_states(world.has_resource<AppStates>()) {}
};

class AppTestPlugin : public Plugin {
  public:
    static inline int setup_count = 0;

    void setup(App& /*app*/) override { ++setup_count; }
};

class StopOnFinishPlugin : public Plugin {
  public:
    static inline int setup_count = 0;
    static inline int finish_count = 0;

    void setup(App& /*app*/) override { ++setup_count; }
    void finish(App& app) override {
        ++finish_count;
        app.resource<AppStates>().should_stop = true;
    }
};

} // namespace fei::app_test
