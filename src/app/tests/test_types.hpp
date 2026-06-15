#pragma once

#include "app/app.hpp"
#include "app/plugin.hpp"

#include <vector>

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

inline AppTestResourceAlias::AppTestResourceAlias(
    AppTestResourceImpl resource
) : stored_value(resource.stored_value) {}

struct FromWorldResource {
    bool had_app_states {false};

    explicit FromWorldResource(World& world) :
        had_app_states(world.has_resource<AppStates>()) {}
};

struct PluginTrace {
    static inline std::vector<int> setup_order {};
    static inline std::vector<int> finish_order {};

    static void reset() {
        setup_order.clear();
        finish_order.clear();
    }
};

class AppTestPlugin : public Plugin {
  public:
    static inline int setup_count = 0;

    void setup(App& /*app*/) override { ++setup_count; }
};

class OrderedPluginA : public Plugin {
  public:
    void setup(App& /*app*/) override { PluginTrace::setup_order.push_back(1); }
    void finish(App& /*app*/) override {
        PluginTrace::finish_order.push_back(1);
    }
};

class OrderedPluginB : public Plugin {
  public:
    void setup(App& /*app*/) override { PluginTrace::setup_order.push_back(2); }
    void finish(App& /*app*/) override {
        PluginTrace::finish_order.push_back(2);
    }
};

class OrderedPluginC : public Plugin {
  public:
    void setup(App& /*app*/) override { PluginTrace::setup_order.push_back(3); }
    void finish(App& /*app*/) override {
        PluginTrace::finish_order.push_back(3);
    }
};

class ConfiguredPlugin : public Plugin {
  public:
    explicit ConfiguredPlugin(int value = 10) : m_value(value) {}

    void setup(App& /*app*/) override {
        PluginTrace::setup_order.push_back(m_value);
    }

  private:
    int m_value;
};

class AppTestPluginGroup : public PluginGroup {
  public:
    PluginGroupBuilder build() override {
        return PluginGroupBuilder::start<AppTestPluginGroup>()
            .add(OrderedPluginA {})
            .add(ConfiguredPlugin {10})
            .add(OrderedPluginB {});
    }
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
