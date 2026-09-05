#pragma once

#include "app/plugin.hpp"
#include "ecs/system_config.hpp"
#include "runtime_protocol/playtest.hpp"
#include "runtime_protocol/playtest_runner.hpp"

namespace ets {

class App;

namespace runtime_protocol {

enum class PlaytestMode : uint8 {
    Automatic,
    Interactive,
    Deterministic,
};

struct PlaytestConfig {
    PlaytestMode mode {PlaytestMode::Automatic};
};

struct PlaytestSystems {
    struct BeginStep : SystemSet<BeginStep> {};
    struct CompleteStep : SystemSet<CompleteStep> {};
};

ETS_REFLECT(Plugin)
class PlaytestPlugin final : public Plugin {
  public:
    void dependencies(PluginDependencies& dependencies) const override;
    void setup(App& app) override;
    void finish(App& app) override;
    void cleanup(App& app) noexcept override;
};

void pause_playtest_clock(World& world);
void resume_playtest_clock(World& world);

[[nodiscard]] Status<PlaytestError> register_playtest_interface(
    App& app,
    PlaytestInterfaceRegistration registration
);

[[nodiscard]] PlaytestRegistry& playtest_registry(App& app);
[[nodiscard]] const PlaytestRegistry& playtest_registry(const App& app);
[[nodiscard]] PlaytestRunner& playtest_runner(App& app);
[[nodiscard]] const PlaytestRunner& playtest_runner(const App& app);

} // namespace runtime_protocol
} // namespace ets
