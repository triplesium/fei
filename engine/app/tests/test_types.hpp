#pragma once

#include "app/app.hpp"
#include "app/plugin.hpp"

#include <stdexcept>
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
    static inline std::vector<int> cleanup_order {};

    static void reset() {
        setup_order.clear();
        finish_order.clear();
        cleanup_order.clear();
    }
};

FEI_REFLECT(Plugin)
class AppTestPlugin : public Plugin {
  public:
    static inline int setup_count = 0;

    void setup(App& /*app*/) override { ++setup_count; }
};

FEI_REFLECT(Plugin(name = ordered))
class OrderedPluginA : public Plugin {
  public:
    void setup(App& /*app*/) override { PluginTrace::setup_order.push_back(1); }
    void finish(App& /*app*/) override {
        PluginTrace::finish_order.push_back(1);
    }
    void cleanup(App& /*app*/) noexcept override {
        PluginTrace::cleanup_order.push_back(1);
    }
};

class OrderedPluginB : public Plugin {
  public:
    void setup(App& /*app*/) override { PluginTrace::setup_order.push_back(2); }
    void finish(App& /*app*/) override {
        PluginTrace::finish_order.push_back(2);
    }
    void cleanup(App& /*app*/) noexcept override {
        PluginTrace::cleanup_order.push_back(2);
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
    static inline int cleanup_count = 0;

    void setup(App& /*app*/) override { ++setup_count; }
    void finish(App& app) override {
        ++finish_count;
        app.resource<AppStates>().should_stop = true;
    }
    void cleanup(App& /*app*/) noexcept override { ++cleanup_count; }
};

inline void throw_during_startup() {
    throw std::runtime_error("startup failure");
}

class ThrowingPlugin : public Plugin {
  public:
    static inline int cleanup_count = 0;

    void setup(App& app) override {
        app.add_systems(StartUp, throw_during_startup);
    }
    void cleanup(App& /*app*/) noexcept override {
        ++cleanup_count;
        PluginTrace::cleanup_order.push_back(9);
    }
};

class RequiredPlugin : public Plugin {
  public:
    explicit RequiredPlugin(int trace_value = 4) : m_trace_value(trace_value) {}

    void setup(App& /*app*/) override {
        PluginTrace::setup_order.push_back(m_trace_value);
    }
    void finish(App& /*app*/) override {
        PluginTrace::finish_order.push_back(m_trace_value);
    }
    void cleanup(App& /*app*/) noexcept override {
        PluginTrace::cleanup_order.push_back(m_trace_value);
    }

  private:
    int m_trace_value;
};

class DependentPlugin : public Plugin {
  public:
    void dependencies(PluginDependencies& dependencies) const override {
        dependencies.require<RequiredPlugin>();
    }

    void setup(App& /*app*/) override { PluginTrace::setup_order.push_back(5); }
    void finish(App& /*app*/) override {
        PluginTrace::finish_order.push_back(5);
    }
    void cleanup(App& /*app*/) noexcept override {
        PluginTrace::cleanup_order.push_back(5);
    }
};

class TransitivePlugin : public Plugin {
  public:
    void dependencies(PluginDependencies& dependencies) const override {
        dependencies.require<DependentPlugin>();
    }

    void setup(App& /*app*/) override { PluginTrace::setup_order.push_back(6); }
};

class NonDefaultPlugin : public Plugin {
  public:
    explicit NonDefaultPlugin(int value) : m_value(value) {}

    void setup(App& /*app*/) override {
        PluginTrace::setup_order.push_back(m_value);
    }

  private:
    int m_value;
};

class RequiresNonDefaultPlugin : public Plugin {
  public:
    void dependencies(PluginDependencies& dependencies) const override {
        dependencies.require<NonDefaultPlugin>();
    }

    void setup(App& /*app*/) override { PluginTrace::setup_order.push_back(7); }
};

class ConfiguredDependentPlugin : public Plugin {
  public:
    void dependencies(PluginDependencies& dependencies) const override {
        dependencies.require(RequiredPlugin {30});
    }

    void setup(App& /*app*/) override { PluginTrace::setup_order.push_back(8); }
};

class CyclePluginB;

class CyclePluginA : public Plugin {
  public:
    void dependencies(PluginDependencies& dependencies) const override;
    void setup(App& /*app*/) override {}
};

class CyclePluginB : public Plugin {
  public:
    void dependencies(PluginDependencies& dependencies) const override {
        dependencies.require<CyclePluginA>();
    }
    void setup(App& /*app*/) override {}
};

inline void CyclePluginA::dependencies(PluginDependencies& dependencies) const {
    dependencies.require<CyclePluginB>();
}

} // namespace fei::app_test
